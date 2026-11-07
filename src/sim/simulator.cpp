#include "vision/sim/Simulator.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace sim {
namespace {

double percentile(std::vector<double> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  std::size_t rank = static_cast<std::size_t>(std::ceil(p * values.size()));
  if (rank == 0) rank = 1;
  if (rank > values.size()) rank = values.size();
  return values[rank - 1];
}

float clampf(float value, float low, float high) {
  return std::max(low, std::min(high, value));
}

bool validate(const ScenarioConfig& config) {
  return config.selected_track_id > 0 && std::isfinite(config.command_horizon_s) &&
         config.command_horizon_s >= 0 && config.noise_px >= 0 && config.frames > 0 && config.dt_s > 0 && config.width > 0 &&
         config.height > 0 && config.target_w > 0 && config.target_h > 0 &&
         config.target_w <= config.width && config.target_h <= config.height &&
         config.epsilon_enter > 0 && config.epsilon_exit > config.epsilon_enter &&
         config.settle_frames > 0 && config.success_after_frame >= 0 &&
         config.unknown_grace_frames > 0 &&
         config.max_age_ms > 0 && config.covariance_limit > 0 &&
         config.actuator_max_speed > 0 && config.actuator_delay_frames >= 0 &&
         config.dropout >= 0 && config.dropout <= 1 &&
         config.false_positive_rate >= 0 && config.false_positive_rate <= 1 &&
         config.low_confidence_rate >= 0 && config.low_confidence_rate <= 1 &&
         config.low_confidence_score >= 0 && config.low_confidence_score <= 1 &&
         config.occlusion_frames >= 0 &&
         (config.occlusion_start < 0 ||
          config.occlusion_start + config.occlusion_frames <= config.frames);
}

}  // namespace

float normalizedError(float dx, float dy, float width, float height) {
  return std::hypot(dx / width, dy / height);
}

const char* name(Motion motion) {
  switch (motion) {
    case Motion::Constant: return "constant";
    case Motion::Stop: return "stop";
    case Motion::Turn: return "turn";
    case Motion::Squat: return "squat";
    case Motion::Occlusion: return "occlusion";
    case Motion::CameraPan: return "camera_pan";
  }
  return "unknown";
}

const char* name(TaskResult result) {
  switch (result) {
    case TaskResult::Running: return "Running";
    case TaskResult::Success: return "SUCCESS";
    case TaskResult::Failed: return "FAILED";
    case TaskResult::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* name(Cause cause) {
  switch (cause) {
    case Cause::None: return "none";
    case Cause::Detection: return "detection";
    case Cause::Association: return "association";
    case Cause::Prediction: return "prediction";
    case Cause::Control: return "control";
    case Cause::Timeout: return "timeout";
  }
  return "unknown";
}

std::string sizeBucket(float width, float height) {
  const float side = std::min(width, height);
  if (side < 16) return "<16";
  if (side < 32) return "16-32";
  if (side < 96) return "32-96";
  return ">96";
}

Simulator::Simulator(ScenarioConfig config, mot::Config tracker)
    : config_(std::move(config)), tracker_(std::move(tracker)) {
  if (!validate(config_)) {
    throw std::invalid_argument("Invalid simulation scenario configuration");
  }
}

ScenarioResult Simulator::run() {
  ScenarioResult output;
  output.scene_id = config_.scene_id;
  output.motion = config_.motion;

  const float dt = static_cast<float>(config_.dt_s);
  std::mt19937 rng(config_.seed);
  std::normal_distribution<float> noise(0.0f, config_.noise_px);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);

  mot::Tracker tracker(tracker_);
  mot::Selection selection;

  float world_x = config_.start_x;
  float world_y = config_.start_y;
  float vx = config_.vx;
  float vy = config_.vy;
  float height = config_.target_h;
  float bottom = world_y + height * 0.5f;
  float camera_x = 0;
  float aim_x = config_.width * 0.5f;
  float aim_y = config_.height * 0.5f;
  float last_est_x = aim_x;
  float last_est_y = aim_y;
  float last_pred_x = aim_x;
  float last_pred_y = aim_y;
  std::deque<std::pair<float, float>> command_queue;

  std::uint64_t last_track_id = 0;
  int id_switches = 0;
  int settle_count = 0;
  int unseen_frames = 0;
  int confirmed_frames = 0;
  int saturated_frames = 0;
  int missed = 0;
  int total_detections = 0;
  int total_false_positives = 0;
  int occlusion_end =
      config_.occlusion_start >= 0
          ? config_.occlusion_start + config_.occlusion_frames
          : -1;
  bool occlusion_recovered = false;
  int occlusion_recovery_frame = -1;
  bool seen_below = false;
  double max_after_below = 0;
  std::vector<double> errors;
  std::vector<double> ages;

  for (int frame = 0; frame < config_.frames; ++frame) {
    const std::int64_t ts_ns =
        static_cast<std::int64_t>(std::llround(frame * config_.dt_s * 1e9));

    if (config_.motion == Motion::Stop && frame >= config_.frames / 3) {
      vx = 0;
      vy = 0;
    }
    if (config_.motion == Motion::Turn && frame == config_.frames / 2) {
      vx = -vx;
      vy = -vy;
      if (vx == 0 && vy == 0) vx = -120;
    }
    if (config_.motion == Motion::Squat && frame >= config_.frames / 3 &&
        height > 24) {
      height = std::max(24.0f, height - 2.0f);
      world_y = bottom - height * 0.5f;
    }
    if (config_.motion == Motion::CameraPan && frame >= config_.frames / 20) {
      camera_x += config_.camera_vx * dt;
    }

    world_x += vx * dt;
    world_y += vy * dt;

    if (config_.motion != Motion::CameraPan) {
      const float min_x = config_.target_w * 0.5f;
      const float max_x = static_cast<float>(config_.width) - config_.target_w * 0.5f;
      const float min_y = config_.target_h * 0.5f;
      const float max_y = static_cast<float>(config_.height) - config_.target_h * 0.5f;
      if (world_x < min_x) { world_x = min_x; vx = -vx; }
      if (world_x > max_x) { world_x = max_x; vx = -vx; }
      if (world_y < min_y) { world_y = min_y; vy = -vy; }
      if (world_y > max_y) { world_y = max_y; vy = -vy; }
    }

    const float truth_x = clampf(world_x - camera_x,
                                 config_.target_w * 0.5f,
                                 static_cast<float>(config_.width) - config_.target_w * 0.5f);
    const float truth_y = clampf(world_y,
                                 config_.target_h * 0.5f,
                                 static_cast<float>(config_.height) - config_.target_h * 0.5f);
    const bool occluded = config_.occlusion_start >= 0 &&
                          frame >= config_.occlusion_start && frame < occlusion_end;

    std::vector<mot::Detection> detections;
    bool detected = false;
    bool false_positive_this_frame = false;
    if (!occluded && unit(rng) >= config_.dropout) {
      detected = true;
      const float sx = truth_x + noise(rng);
      const float sy = truth_y + noise(rng);
      const float score = unit(rng) < config_.low_confidence_rate
                              ? config_.low_confidence_score
                              : 0.9f;
      const cv::Rect2f person{sx - config_.target_w * 0.5f,
                              sy - height * 0.5f,
                              config_.target_w, height};
      detections.push_back({1, score, person});

      const float head_h = std::max(6.0f, height * 0.22f);
      const float head_w = std::max(4.0f, config_.target_w * 0.45f);
      const float head_cy = sy - height * 0.5f + head_h * 0.55f;
      const cv::Rect2f head{sx - head_w * 0.5f, head_cy - head_h * 0.5f,
                            head_w, head_h};
      detections.push_back({0, score, head});
    } else if (!occluded) {
      ++missed;
    }

    if (unit(rng) < config_.false_positive_rate) {
      const float fp_x = 20.0f + unit(rng) * (config_.width - 40.0f);
      const float fp_y = 20.0f + unit(rng) * (config_.height - 40.0f);
      detections.push_back({1, 0.5f,
                            {fp_x - 10.0f, fp_y - 20.0f, 20.0f, 40.0f}});
      ++total_false_positives;
      false_positive_this_frame = true;
    }

    total_detections += static_cast<int>(detections.size());
    if (!tracker.update(frame, ts_ns, detections)) {
      throw std::runtime_error("Simulation produced out-of-order frames");
    }

    const auto& snapshot = tracker.snapshot();
    const mot::Track* selected = nullptr;
    if (!selection.selected) {
      if (config_.acquire_single_confirmed) {
        const mot::Track* candidate = nullptr;
        int eligible = 0;
        for (const auto& track : snapshot.tracks) {
          if (track.state == mot::State::Confirmed && !track.ambiguous) {
            candidate = &track;
            ++eligible;
          }
        }
        if (eligible == 1) selection.select(candidate->id, snapshot);
      } else {
        selection.select(static_cast<mot::Id>(config_.selected_track_id), snapshot);
      }
    }
    for (const auto& track : snapshot.tracks) {
      if (selection.selected == track.id) {
        selected = &track;
        break;
      }
    }

    if (selected && last_track_id != 0 && selected->id != last_track_id) {
      ++id_switches;
    }
    if (selected) last_track_id = selected->id;
    if (selected && selected->state == mot::State::Confirmed) ++confirmed_frames;

    const bool has_track = selected != nullptr;
    const std::uint64_t track_id = selected ? selected->id : 0;
    const bool ambiguous = selected ? selected->ambiguous : true;
    const float age_ms =
        selected ? static_cast<float>((ts_ns - selected->lastSeen) * 1e-6) : 0;
    const double covariance_trace =
        selected ? selected->covariance(0, 0) + selected->covariance(1, 1) : 0;

    float estimate_x = last_est_x;
    float estimate_y = last_est_y;
    float predicted_x = last_pred_x;
    float predicted_y = last_pred_y;
    if (selected) {
      estimate_x = static_cast<float>(selected->x[0]);
      estimate_y = static_cast<float>(selected->x[1]);
      predicted_x = estimate_x + static_cast<float>(selected->x[2]) *
                                     config_.command_horizon_s;
      predicted_y = estimate_y + static_cast<float>(selected->x[3]) *
                                     config_.command_horizon_s;
      last_est_x = estimate_x;
      last_est_y = estimate_y;
      last_pred_x = predicted_x;
      last_pred_y = predicted_y;
    }
    predicted_x = clampf(predicted_x, 0, static_cast<float>(config_.width));
    predicted_y = clampf(predicted_y, 0, static_cast<float>(config_.height));

    // Only actionable observations may produce a new control command.
    const bool controllable = selected && selected->state == mot::State::Confirmed &&
                              !selected->ambiguous && selected->lastSeen == ts_ns &&
                              age_ms <= config_.max_age_ms &&
                              covariance_trace <= config_.covariance_limit;
    std::pair<float,float> target{aim_x,aim_y};
    if (controllable) {
      const float horizon = config_.compensate_actuator_delay
                                ? config_.actuator_delay_frames * dt
                                : config_.command_horizon_s;
      target.first = clampf(estimate_x + static_cast<float>(selected->x[2])*horizon, 0,
                            static_cast<float>(config_.width));
      target.second = clampf(estimate_y + static_cast<float>(selected->x[3])*horizon, 0,
                             static_cast<float>(config_.height));
    }
    command_queue.push_back(target);
    std::pair<float,float> command{aim_x,aim_y};
    if (command_queue.size() > static_cast<size_t>(config_.actuator_delay_frames)) {
      command = command_queue.front();
      command_queue.pop_front();
    }
    const float max_step = config_.actuator_max_speed * dt;
    const float dx = command.first - aim_x;
    const float dy = command.second - aim_y;
    const float distance = std::hypot(dx, dy);
    if (distance > max_step) {
      aim_x += dx / distance * max_step;
      aim_y += dy / distance * max_step;
      ++saturated_frames;
    } else {
      aim_x = command.first;
      aim_y = command.second;
    }

    const float error_x = aim_x - truth_x;
    const float error_y = aim_y - truth_y;
    const float error_px = std::hypot(error_x, error_y);
    const float error_norm = normalizedError(error_x, error_y, config_.target_w, height);

    const bool valid = controllable;

    FrameRecord record;
    record.frame = frame;
    record.ts_ns = ts_ns;
    record.truth_x = truth_x;
    record.truth_y = truth_y;
    record.occluded = occluded;
    record.observed = detected;
    record.has_track = has_track;
    record.track_id = track_id;
    record.track_state = selected ? mot::name(selected->state) : "None";
    record.ambiguous = ambiguous;
    record.est_x = estimate_x;
    record.est_y = estimate_y;
    record.pred_x = predicted_x;
    record.pred_y = predicted_y;
    record.aim_x = aim_x;
    record.aim_y = aim_y;
    record.error_px = error_px;
    record.error_norm = error_norm;
    record.age_ms = age_ms;
    record.covariance_trace = covariance_trace;
    record.detections = static_cast<int>(detections.size());
    record.false_positives = false_positive_this_frame ? 1 : 0;
    output.frames.push_back(record);

    errors.push_back(error_px);
    if (selected) {
      ages.push_back(age_ms);
    }
    if (error_norm < config_.epsilon_enter) {
      seen_below = true;
    }
    if (seen_below) {
      float directionScale = error_norm > 0 ? error_px / error_norm : 0;
      double overshoot = std::max(0.0f, error_px - config_.epsilon_enter * directionScale);
      max_after_below = std::max(max_after_below, overshoot);
    }

    if (config_.occlusion_start >= 0 && !occlusion_recovered &&
        frame >= occlusion_end && valid &&
        error_norm < config_.epsilon_enter) {
      occlusion_recovered = true;
      occlusion_recovery_frame = frame;
    }

    if (frame >= config_.success_after_frame && valid &&
        error_norm < config_.epsilon_enter) {
      ++settle_count;
      if (settle_count >= config_.settle_frames && output.result == TaskResult::Running) {
        output.result = TaskResult::Success;
        output.terminal_frame = frame;
      }
    } else if (frame < config_.success_after_frame || !valid ||
               error_norm > config_.epsilon_exit) {
      settle_count = 0;
    }

    if (!valid) {
      ++unseen_frames;
      if (unseen_frames >= config_.unknown_grace_frames && output.result == TaskResult::Running) {
        output.result = TaskResult::Unknown;
        output.cause = Cause::Detection;
        output.terminal_frame = frame;
      }
    } else {
      unseen_frames = 0;
    }
  }

  if (output.result == TaskResult::Running) {
    output.result = TaskResult::Failed;
    output.terminal_frame = static_cast<int>(output.frames.size()) - 1;
    if (confirmed_frames == 0) {
      output.cause = Cause::Detection;
    } else if (saturated_frames > config_.frames / 2) {
      output.cause = Cause::Control;
    } else if (id_switches > 0) {
      output.cause = Cause::Association;
    } else {
      output.cause = Cause::Timeout;
    }
  }

  output.metrics.frames = static_cast<int>(output.frames.size());
  output.metrics.mae_px = 0;
  if (!errors.empty()) {
    double sum = 0;
    for (double error : errors) sum += error;
    output.metrics.mae_px = sum / errors.size();
  }
  double estimate_sum = 0, prediction_sum = 0;
  int prediction_opportunities = 0;
  for (const auto& record : output.frames) {
    if (record.has_track) {
      ++output.metrics.tracked_frames;
      estimate_sum += std::hypot(record.est_x-record.truth_x, record.est_y-record.truth_y);
    }
    double query = record.frame + config_.command_horizon_s/config_.dt_s;
    if (query > output.frames.size()-1) continue;
    ++prediction_opportunities;
    if (!record.has_track) continue;
    auto lo = static_cast<size_t>(std::floor(query));
    auto hi = std::min(lo+1, output.frames.size()-1);
    double alpha = query-lo;
    double x = (1-alpha)*output.frames[lo].truth_x+alpha*output.frames[hi].truth_x;
    double y = (1-alpha)*output.frames[lo].truth_y+alpha*output.frames[hi].truth_y;
    prediction_sum += std::hypot(record.pred_x-x,record.pred_y-y);
    ++output.metrics.prediction_samples;
  }
  auto& metrics = output.metrics;
  metrics.track_coverage = double(metrics.tracked_frames)/config_.frames;
  if (metrics.tracked_frames) metrics.estimate_mae_px = estimate_sum/metrics.tracked_frames;
  if (metrics.prediction_samples) metrics.prediction_mae_px = prediction_sum/metrics.prediction_samples;
  if (prediction_opportunities) metrics.prediction_coverage =
      double(metrics.prediction_samples)/prediction_opportunities;
  output.metrics.p95_px = percentile(errors, 0.95);
  output.metrics.settling_s =
      output.result == TaskResult::Success ? output.terminal_frame * config_.dt_s : -1;
  output.metrics.overshoot_px =
      seen_below ? max_after_below : 0;
  output.metrics.occlusion_recovery_s =
      occlusion_recovered
          ? (occlusion_recovery_frame - occlusion_end) * config_.dt_s
          : -1;
  output.metrics.age_p50_ms = percentile(ages, 0.50);
  output.metrics.age_p95_ms = percentile(ages, 0.95);
  output.metrics.age_p99_ms = percentile(ages, 0.99);
  output.metrics.detections = total_detections;
  output.metrics.false_positives = total_false_positives;
  output.metrics.missed = missed;
  output.metrics.id_switches = id_switches;
  return output;
}

}  // namespace sim
