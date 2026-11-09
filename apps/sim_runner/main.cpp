#include "vision/sim/Simulator.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/persistence.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Group {
  int total = 0;
  int success = 0;
  int failed = 0;
  int unknown = 0;
  double mae_sum = 0;
};

sim::AssistMode parseAssistMode(const std::string& value) {
  if (value == "legacy") return sim::AssistMode::Legacy;
  if (value == "human_led") return sim::AssistMode::HumanLed;
  throw std::runtime_error("Unknown assist mode: " + value);
}

sim::Motion parseMotion(const std::string& value) {
  if (value == "constant") return sim::Motion::Constant;
  if (value == "stop") return sim::Motion::Stop;
  if (value == "turn") return sim::Motion::Turn;
  if (value == "squat") return sim::Motion::Squat;
  if (value == "occlusion") return sim::Motion::Occlusion;
  if (value == "camera_pan") return sim::Motion::CameraPan;
  throw std::runtime_error("Unknown simulation motion: " + value);
}

int intOr(const cv::FileNode& node, const char* key, int fallback) {
  return node[key].empty() ? fallback : static_cast<int>(node[key]);
}

float floatOr(const cv::FileNode& node, const char* key, float fallback) {
  return node[key].empty() ? fallback : static_cast<float>(node[key]);
}

double doubleOr(const cv::FileNode& node,const char* key,double fallback) {
  return node[key].empty() ? fallback : static_cast<double>(node[key]);
}

std::string stringOr(const cv::FileNode& node, const char* key,
                     const std::string& fallback) {
  return node[key].empty() ? fallback : static_cast<std::string>(node[key]);
}

std::vector<sim::ScenarioConfig> loadScenarios(const std::string& path) {
  cv::FileStorage file(path, cv::FileStorage::READ);
  if (!file.isOpened()) throw std::runtime_error("Cannot open simulation config: " + path);
  const cv::FileNode scenarios = file["scenarios"];
  if (scenarios.empty() || !scenarios.isSeq()) {
    throw std::runtime_error("Simulation config needs a scenarios sequence");
  }
  const cv::FileNode defaults = file["assist_defaults"];
  auto pickDouble = [&](const cv::FileNode& node, const char* key, double fallback) {
    if (!node[key].empty()) return static_cast<double>(node[key]);
    if (!defaults.empty() && !defaults[key].empty()) return static_cast<double>(defaults[key]);
    return fallback;
  };
  auto pickInt = [&](const cv::FileNode& node, const char* key, int fallback) {
    if (!node[key].empty()) return static_cast<int>(node[key]);
    if (!defaults.empty() && !defaults[key].empty()) return static_cast<int>(defaults[key]);
    return fallback;
  };
  auto pickString = [&](const cv::FileNode& node, const char* key, const std::string& fallback) {
    if (!node[key].empty()) return static_cast<std::string>(node[key]);
    if (!defaults.empty() && !defaults[key].empty()) return static_cast<std::string>(defaults[key]);
    return fallback;
  };
  std::vector<sim::ScenarioConfig> result;
  for (auto it = scenarios.begin(); it != scenarios.end(); ++it) {
    const cv::FileNode node = *it;
    sim::ScenarioConfig config;
    config.scene_id = stringOr(node, "id", "scene");
    config.motion = parseMotion(stringOr(node, "motion", "constant"));
    config.acquire_single_confirmed = intOr(node, "acquire_single_confirmed", 0) != 0;
    config.compensate_actuator_delay = intOr(node, "compensate_actuator_delay", 0) != 0;
    config.selected_track_id = intOr(node, "selected_track_id", config.selected_track_id);
    config.frames = intOr(node, "frames", config.frames);
    config.dt_s = doubleOr(node, "dt_s", config.dt_s);
    config.width = intOr(node, "width", config.width);
    config.height = intOr(node, "height", config.height);
    config.target_w = floatOr(node, "target_w", config.target_w);
    config.target_h = floatOr(node, "target_h", config.target_h);
    config.start_x = floatOr(node, "start_x", config.start_x);
    config.start_y = floatOr(node, "start_y", config.start_y);
    config.vx = floatOr(node, "vx", config.vx);
    config.vy = floatOr(node, "vy", config.vy);
    config.camera_vx = floatOr(node, "camera_vx", config.camera_vx);
    config.noise_px = floatOr(node, "noise_px", config.noise_px);
    config.dropout = floatOr(node, "dropout", config.dropout);
    config.occlusion_start = intOr(node, "occlusion_start", config.occlusion_start);
    config.occlusion_frames = intOr(node, "occlusion_frames", config.occlusion_frames);
    config.false_positive_rate =
        floatOr(node, "false_positive_rate", config.false_positive_rate);
    config.low_confidence_rate =
        floatOr(node, "low_confidence_rate", config.low_confidence_rate);
    config.low_confidence_score =
        floatOr(node, "low_confidence_score", config.low_confidence_score);
    config.epsilon_enter = floatOr(node, "epsilon_enter", config.epsilon_enter);
    config.epsilon_exit = floatOr(node, "epsilon_exit", config.epsilon_exit);
    config.settle_frames = intOr(node, "settle_frames", config.settle_frames);
    config.success_after_frame =
        intOr(node, "success_after_frame", config.success_after_frame);
    config.covariance_limit = floatOr(node, "covariance_limit", config.covariance_limit);
    config.max_age_ms = floatOr(node, "max_age_ms", config.max_age_ms);
    config.actuator_max_speed =
        floatOr(node, "actuator_max_speed", config.actuator_max_speed);
    config.actuator_delay_frames =
        intOr(node, "actuator_delay_frames", config.actuator_delay_frames);
    config.command_horizon_s =
        floatOr(node, "command_horizon_s", config.command_horizon_s);
    config.unknown_grace_frames =
        intOr(node, "unknown_grace_frames", config.unknown_grace_frames);
    config.feedback_prediction=intOr(node,"feedback_prediction",0)!=0;
    config.encoder_compensation=intOr(node,"encoder_compensation",0)!=0;
    config.encoder_transport=intOr(node,"encoder_transport",0)!=0;
    config.encoder_extrapolation=intOr(node,"encoder_extrapolation",0)!=0;
    config.velocity_feedforward=intOr(node,"velocity_feedforward",0)!=0;
    config.encoder_delay_compensation=intOr(node,"encoder_delay_compensation",0)!=0;
    config.async_stages=intOr(node,"async_stages",0)!=0;
    config.encoder_delay_ms=doubleOr(node,"encoder_delay_ms",0);
    config.encoder_drop_rate=doubleOr(node,"encoder_drop_rate",0);
    config.encoder_max_age_ms=doubleOr(node,"encoder_max_age_ms",200);
    config.encoder_drop_end_frame=intOr(node,"encoder_drop_end_frame",-1);
    config.view_feedback = intOr(node,"view_feedback",0)!=0;
    config.incremental_control = intOr(node, "incremental_control", 0)!=0;
    config.pid_kp=doubleOr(node,"pid_kp",config.pid_kp);
    config.pid_ki=doubleOr(node,"pid_ki",config.pid_ki);
    config.pid_kd=doubleOr(node,"pid_kd",config.pid_kd);
    config.actuator_units_per_px=doubleOr(node,"actuator_units_per_px",config.actuator_units_per_px);
    config.disconnect_start=intOr(node,"disconnect_start",config.disconnect_start);
    config.disconnect_frames=intOr(node,"disconnect_frames",config.disconnect_frames);
    config.dispatch_failure_rate=doubleOr(node,"dispatch_failure_rate",config.dispatch_failure_rate);
    config.feedback_drop_rate=doubleOr(node,"feedback_drop_rate",config.feedback_drop_rate);
    config.feedback_timeout_ms=doubleOr(node,"feedback_timeout_ms",config.feedback_timeout_ms);
    config.feedback_drop_end_frame=intOr(node,"feedback_drop_end_frame",config.feedback_drop_end_frame);
    config.feedback_resync_frame=intOr(node,"feedback_resync_frame",config.feedback_resync_frame);
    config.feedback_transport=intOr(node,"feedback_transport",0)!=0;
    config.feedback_delay_ms=doubleOr(node,"feedback_delay_ms",0);
    config.stop_request_delay_ms=doubleOr(node,"stop_request_delay_ms",0);
    config.feedback_jitter_ms=doubleOr(node,"feedback_jitter_ms",0);
    config.feedback_capacity=intOr(node,"feedback_capacity",config.feedback_capacity);
    config.observation_capacity=intOr(node,"observation_capacity",config.observation_capacity);
    config.observation_jitter_ms=doubleOr(node,"observation_jitter_ms",config.observation_jitter_ms);
    config.inference_service_ms=doubleOr(node,"inference_service_ms",config.inference_service_ms);
    config.observation_delay_ms=doubleOr(node,"observation_delay_ms",config.observation_delay_ms);
    config.delay_jitter_ms=doubleOr(node,"delay_jitter_ms",config.delay_jitter_ms);
    config.seed = static_cast<std::uint32_t>(intOr(node, "seed", 1));
    config.assist_mode = parseAssistMode(pickString(node, "assist_mode", "legacy"));
    config.assist_enter_px = pickDouble(node, "assist_enter_px", config.assist_enter_px);
    config.assist_exit_px = pickDouble(node, "assist_exit_px", config.assist_exit_px);
    config.assist_deadzone_px = pickDouble(node, "assist_deadzone_px", config.assist_deadzone_px);
    config.assist_max_step_px = pickDouble(node, "assist_max_step_px", config.assist_max_step_px);
    config.assist_max_speed_px_s = pickDouble(node, "assist_max_speed_px_s", config.assist_max_speed_px_s);
    config.assist_max_accel_px_s2 = pickDouble(node, "assist_max_accel_px_s2", config.assist_max_accel_px_s2);
    config.human_turn_px = pickDouble(node, "human_turn_px", config.human_turn_px);
    config.assist_resume_wait_s = pickDouble(node, "assist_resume_wait_s", config.assist_resume_wait_s);
    config.assist_center_x = pickDouble(node, "assist_center_x", config.assist_center_x);
    config.assist_center_y = pickDouble(node, "assist_center_y", config.assist_center_y);
    config.assist_enabled = pickInt(node, "assist_enabled", config.assist_enabled ? 1 : 0) != 0;
    config.distractor = pickInt(node, "distractor", 0) != 0;
    config.distractor_x = static_cast<float>(pickDouble(node, "distractor_x", config.distractor_x));
    config.distractor_y = static_cast<float>(pickDouble(node, "distractor_y", config.distractor_y));
    config.distractor_vx = static_cast<float>(pickDouble(node, "distractor_vx", config.distractor_vx));
    config.distractor_vy = static_cast<float>(pickDouble(node, "distractor_vy", config.distractor_vy));
    config.distractor_w = static_cast<float>(pickDouble(node, "distractor_w", config.distractor_w));
    config.distractor_h = static_cast<float>(pickDouble(node, "distractor_h", config.distractor_h));
    config.image_detector = stringOr(node, "image_detector", "");
    const cv::FileNode human = node["human_camera"];
    if (!human.empty()) {
      if (!human.isSeq()) throw std::runtime_error("human_camera must be a sequence");
      for (auto sample = human.begin(); sample != human.end(); ++sample) {
        sim::HumanCameraSample input;
        input.time_ns = std::llround(doubleOr(*sample, "t_ns", 0));
        input.dx_px = doubleOr(*sample, "dx_px", 0);
        input.dy_px = doubleOr(*sample, "dy_px", 0);
        input.takeover = intOr(*sample, "takeover", 0) != 0;
        config.human_camera.push_back(input);
      }
    }
    result.push_back(config);
  }
  if (result.empty()) throw std::runtime_error("Simulation config has no scenarios");
  return result;
}

std::string jsonString(const std::string& value) {
  std::string escaped = "\"";
  for (char character : value) {
    switch (character) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += character; break;
    }
  }
  return escaped + "\"";
}

void writeFrame(std::ostream& out, const std::string& scene,
                const sim::FrameRecord& frame) {
  out << "{\"scene\":" << jsonString(scene)
      << ",\"frame\":" << frame.frame
      << ",\"observation_ts_ns\":" << frame.observation_ts_ns
      << ",\"observation_frame\":" << frame.observation_frame
      << ",\"ts_ns\":" << frame.ts_ns
      << ",\"estimated_delay_ms\":" << frame.estimated_delay_ms
      << ",\"feedback_dropped\":" << frame.feedback_dropped
      << ",\"encoder_valid\":" << (frame.encoder_valid?"true":"false")
      << ",\"encoder_sample_ts_ns\":" << frame.encoder_sample_ts_ns
      << ",\"observation_age_ms\":" << frame.observation_age_ms
      << ",\"encoder_age_ms\":" << frame.encoder_age_ms
      << ",\"command_lead_ms\":" << frame.command_lead_ms
      << ",\"encoder_dropped\":" << frame.encoder_dropped
      << ",\"encoder_overflow\":" << frame.encoder_overflow
      << ",\"encoder_queued\":" << frame.encoder_queued
      << ",\"feedback_pending\":" << frame.feedback_pending
      << ",\"feedback_fault\":" << (frame.feedback_fault?"true":"false")
      << ",\"feedback_resynced\":" << (frame.feedback_resynced?"true":"false")
      << ",\"feedback_sync_attempts\":" << frame.feedback_sync_attempts
      << ",\"feedback_delivered\":" << frame.feedback_delivered
      << ",\"feedback_ignored\":" << frame.feedback_ignored
      << ",\"feedback_invalid\":" << frame.feedback_invalid
      << ",\"feedback_overflow\":" << frame.feedback_overflow
      << ",\"feedback_queued\":" << frame.feedback_queued
      << ",\"feedback_epoch\":" << frame.feedback_epoch
      << ",\"camera_x\":" << frame.camera_x
      << ",\"camera_y\":" << frame.camera_y
      << ",\"truth_x\":" << frame.truth_x
      << ",\"truth_y\":" << frame.truth_y
      << ",\"occluded\":" << (frame.occluded ? "true" : "false")
      << ",\"observed\":" << (frame.observed ? "true" : "false")
      << ",\"has_track\":" << (frame.has_track ? "true" : "false")
      << ",\"track_id\":" << frame.track_id
      << ",\"control_gate\":" << jsonString(frame.control_gate)
      << ",\"control_valid\":" << (frame.control_valid?"true":"false")
      << ",\"track_state\":" << jsonString(frame.track_state)
      << ",\"ambiguous\":" << (frame.ambiguous ? "true" : "false")
      << ",\"est_x\":" << frame.est_x
      << ",\"est_y\":" << frame.est_y
      << ",\"est_vx\":" << frame.est_vx
      << ",\"est_vy\":" << frame.est_vy
      << ",\"pred_x\":" << frame.pred_x
      << ",\"pred_y\":" << frame.pred_y
      << ",\"aim_x\":" << frame.aim_x
      << ",\"aim_y\":" << frame.aim_y
      << ",\"error_px\":" << frame.error_px
      << ",\"error_norm\":" << frame.error_norm
      << ",\"age_ms\":" << frame.age_ms
      << ",\"covariance_trace\":" << frame.covariance_trace
      << ",\"detections\":" << frame.detections
      << ",\"false_positives\":" << frame.false_positives
      << ",\"assist_state\":" << jsonString(frame.assist_state)
      << ",\"assist_reason\":" << jsonString(frame.assist_reason)
      << ",\"assist_cancel\":" << (frame.assist_cancel ? "true" : "false")
      << ",\"assist_dx_px\":" << frame.assist_dx_px
      << ",\"assist_dy_px\":" << frame.assist_dy_px
      << ",\"human_takeover\":" << (frame.human_takeover ? "true" : "false")
      << ",\"human_sample\":" << (frame.human_sample ? "true" : "false")
      << ",\"human_explicit\":" << (frame.human_explicit ? "true" : "false")
      << ",\"human_release\":" << (frame.human_release ? "true" : "false")
      << ",\"human_dx_px\":" << frame.human_dx_px
      << ",\"human_dy_px\":" << frame.human_dy_px
      << ",\"executed_dx_px\":" << frame.executed_dx_px
      << ",\"executed_dy_px\":" << frame.executed_dy_px
      << ",\"auto_follow\":" << (frame.auto_follow ? "true" : "false")
      << ",\"feed_active\":" << (frame.feed_active ? "true" : "false")
      << ",\"used_horizon_s\":" << frame.used_horizon_s
      << ",\"gate_transform\":" << jsonString(frame.gate_transform);
  if(frame.has_modes) {
    out<<",\"mode_probabilities\":[";
    for(size_t j=0;j<3;++j) out<<(j?",":"")<<frame.mode_probabilities[j];
    out<<"]";
  }
  out<<"}\n";
}

std::string metricJson(const sim::Metrics& metrics) {
  std::ostringstream out;
  out.precision(9);
  out << "{\"mae_px\":" << metrics.mae_px
      << ",\"success_maintained\":" << (metrics.success_maintained?"true":"false")
      << ",\"post_success_frames\":" << metrics.post_success_frames
      << ",\"post_success_violations\":" << metrics.post_success_violations
      << ",\"first_post_success_violation\":" << metrics.first_post_success_violation
      << ",\"end_stable\":" << (metrics.end_stable?"true":"false")
      << ",\"end_stable_frames\":" << metrics.end_stable_frames
      << ",\"control_mae_px\":" << metrics.mae_px
      << ",\"tracked_frames\":" << metrics.tracked_frames
      << ",\"track_coverage\":" << metrics.track_coverage
      << ",\"estimate_mae_px\":" << (metrics.tracked_frames ? std::to_string(metrics.estimate_mae_px) : "null")
      << ",\"prediction_samples\":" << metrics.prediction_samples
      << ",\"prediction_coverage\":" << metrics.prediction_coverage
      << ",\"prediction_mae_px\":" << (metrics.prediction_samples ? std::to_string(metrics.prediction_mae_px) : "null")
      << ",\"p95_px\":" << metrics.p95_px
      << ",\"settling_s\":" << metrics.settling_s
      << ",\"overshoot_px\":" << metrics.overshoot_px
      << ",\"occlusion_recovery_s\":" << metrics.occlusion_recovery_s
      << ",\"age_p50_ms\":" << metrics.age_p50_ms
      << ",\"age_p95_ms\":" << metrics.age_p95_ms
      << ",\"age_p99_ms\":" << metrics.age_p99_ms
      << ",\"frames\":" << metrics.frames
      << ",\"detections\":" << metrics.detections
      << ",\"false_positives\":" << metrics.false_positives
      << ",\"missed\":" << metrics.missed
      << ",\"id_switches\":" << metrics.id_switches << "}";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 3 || argc > 4) {
    throw std::runtime_error(
        "usage: sim_runner scenarios.yml new-output-directory [tracker.yml]");
  }
  const fs::path executable_dir = fs::absolute(argv[0]).parent_path();
  const std::string tracker_path =
      argc == 4 ? argv[3] : (executable_dir / "tracker.yml").string();
  const auto scenarios = loadScenarios(argv[1]);
  const mot::Config tracker_config = mot::Config::load(tracker_path);

  const fs::path output(argv[2]);
  if (fs::exists(output)) {
    throw std::runtime_error("Use a new output directory");
  }
  fs::create_directories(output);

  std::ofstream runs(output / "runs.jsonl");
  runs.exceptions(std::ios::badbit | std::ios::failbit);
  runs.precision(9);

  std::ofstream commands(output / "commands.jsonl");
  commands.exceptions(std::ios::badbit | std::ios::failbit);
  commands.precision(17);
  std::ofstream feedback_log(output / "feedback.jsonl");
  feedback_log.exceptions(std::ios::badbit | std::ios::failbit);feedback_log.precision(17);
  std::vector<sim::ScenarioResult> results;
  std::map<std::string, Group> by_motion;
  std::map<std::string, Group> by_size;
  int success = 0;
  int failed = 0;
  int unknown = 0;
  bool all_terminal = true;

  for (const auto& scenario : scenarios) {
    sim::ImageObservation images;
    if (scenario.image_detector == "pixel") {
      images.sprite = cv::Mat(std::max(1, cvRound(scenario.target_h)),
                              std::max(1, cvRound(scenario.target_w)), CV_8UC3, cv::Scalar(0, 255, 0));
      images.background = cv::Mat(scenario.height, scenario.width, CV_8UC3, cv::Scalar(40, 40, 40));
      images.detect = [](const cv::Mat& image, int, std::int64_t) {
        cv::Mat mask;
        cv::inRange(image, cv::Scalar(0, 180, 0), cv::Scalar(80, 255, 80), mask);
        std::vector<cv::Point> points;
        cv::findNonZero(mask, points);
        std::vector<mot::Detection> boxes;
        if (!points.empty()) boxes.push_back({1, 0.99f, cv::Rect2f(cv::boundingRect(points))});
        return boxes;
      };
    }
    sim::Simulator simulator(scenario, tracker_config, images);
    sim::ScenarioResult result = simulator.run();
    for (const auto& frame : result.frames) {
      writeFrame(runs, result.scene_id, frame);
    }
    for(const auto& e:result.commands) {
      commands << "{\"scene\":" << jsonString(result.scene_id)
        << ",\"kind\":" << jsonString(e.kind) << ",\"id\":" << e.id
        << ",\"frame_id\":" << e.frame_id << ",\"track_id\":" << e.track_id
        << ",\"timestamp\":" << e.timestamp << ",\"generated\":" << e.generated
        << ",\"arrival\":" << e.arrival << ",\"expires\":" << e.expires
        << ",\"dx\":" << e.dx << ",\"dy\":" << e.dy << "}\n";
    }
    for(const auto& trace:result.feedback_messages) {
      const auto& m=trace.snapshot;
      feedback_log << "{\"scene\":" << jsonString(result.scene_id)
        << ",\"epoch\":" << m.epoch << ",\"id\":" << m.command_id << ",\"sequence\":" << m.sequence
        << ",\"reported\":" << m.reported << ",\"accepted_at\":" << m.accepted_at
        << ",\"ready\":" << trace.ready << ",\"received\":" << trace.received
        << ",\"applied_x\":" << m.applied_x << ",\"applied_y\":" << m.applied_y
        << ",\"terminal\":" << (m.terminal?"true":"false") << ",\"status\":" << jsonString(trace.status) << "}\n";
    }
    results.push_back(std::move(result));

    const auto& stored = results.back();
    all_terminal = all_terminal && stored.result != sim::TaskResult::Running;
    Group& motion_group = by_motion[sim::name(stored.motion)];
    Group& size_group = by_size[sim::sizeBucket(scenario.target_w, scenario.target_h)];
    for (Group* group : {&motion_group, &size_group}) {
      ++group->total;
      group->mae_sum += stored.metrics.mae_px;
      if (stored.result == sim::TaskResult::Success) ++group->success;
      else if (stored.result == sim::TaskResult::Failed) ++group->failed;
      else if (stored.result == sim::TaskResult::Unknown) ++group->unknown;
    }
    if (stored.result == sim::TaskResult::Success) ++success;
    else if (stored.result == sim::TaskResult::Failed) ++failed;
    else if (stored.result == sim::TaskResult::Unknown) ++unknown;
  }
  runs.close();

  std::ofstream summary(output / "summary.json");
  summary.exceptions(std::ios::badbit | std::ios::failbit);
  summary.precision(9);
  summary << "{\"schema_version\":3,\"evaluation_window\":\"full_scenario\",\"result_semantics\":\"first_terminal_event_latched\",\"scenarios\":[";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    if (i) summary << ",";
    summary << "{\"id\":" << jsonString(result.scene_id)
            << ",\"motion\":" << jsonString(sim::name(result.motion))
            << ",\"size_bucket\":"
            << jsonString(sim::sizeBucket(scenarios[i].target_w, scenarios[i].target_h))
            << ",\"result\":" << jsonString(sim::name(result.result))
            << ",\"cause\":" << jsonString(sim::name(result.cause))
            << ",\"acquire_single_confirmed\":" << (scenarios[i].acquire_single_confirmed ? "true" : "false")
            << ",\"compensate_actuator_delay\":" << (scenarios[i].compensate_actuator_delay ? "true" : "false")
            << ",\"incremental_control\":" << (scenarios[i].incremental_control ? "true" : "false")
            << ",\"view_feedback\":" << (scenarios[i].view_feedback ? "true" : "false")
            << ",\"encoder_compensation\":" << (scenarios[i].encoder_compensation ? "true" : "false")
            << ",\"encoder_transport\":" << (scenarios[i].encoder_transport?"true":"false")
            << ",\"encoder_extrapolation\":" << (scenarios[i].encoder_extrapolation?"true":"false")
            << ",\"velocity_feedforward\":" << (scenarios[i].velocity_feedforward?"true":"false")
            << ",\"encoder_delay_compensation\":" << (scenarios[i].encoder_delay_compensation?"true":"false")
            << ",\"assist\":{\"mode\":" << jsonString(result.assist.mode)
            << ",\"protocol\":" << jsonString(result.assist.protocol)
            << ",\"parameters_experimental\":" << (result.assist.parameters_experimental?"true":"false")
            << ",\"center_x\":" << result.assist.center_x
            << ",\"center_y\":" << result.assist.center_y
            << ",\"enter_px\":" << result.assist.enter_px
            << ",\"exit_px\":" << result.assist.exit_px
            << ",\"deadzone_px\":" << result.assist.deadzone_px
            << ",\"max_step_px\":" << result.assist.max_step_px
            << ",\"max_speed_px_s\":" << result.assist.max_speed_px_s
            << ",\"max_accel_px_s2\":" << result.assist.max_accel_px_s2
            << ",\"human_turn_px\":" << result.assist.human_turn_px
            << ",\"resume_wait_s\":" << result.assist.resume_wait_s
            << ",\"human_samples\":" << result.assist.human_samples
            << ",\"enabled\":" << (result.assist.enabled ? "true" : "false")
            << ",\"displacement_unit\":" << jsonString(result.assist.displacement_unit)
            << ",\"speed_unit\":" << jsonString(result.assist.speed_unit)
            << ",\"acceleration_unit\":" << jsonString(result.assist.acceleration_unit)
            << ",\"time_unit\":" << jsonString(result.assist.time_unit)
            << "}"
            << ",\"effective\":{\"delay_compensation_active\":" << (result.effective.delay_compensation_active?"true":"false")
            << ",\"measurement_sigma\":" << result.effective.measurement_sigma
            << ",\"acceleration_sigma\":" << result.effective.acceleration_sigma
            << ",\"maneuver_radius\":" << result.effective.maneuver_radius
            << ",\"maneuver_acceleration\":" << result.effective.maneuver_acceleration
            << ",\"covariance_limit\":" << result.effective.covariance_limit
            << ",\"gate_covariance\":" << (result.effective.gate_uses_predicted_covariance?"\"predicted\"":"\"measurement\"")
            << ",\"velocity_feedforward\":" << (result.effective.velocity_feedforward?"true":"false")
            << ",\"auto_follow_frames\":" << result.effective.auto_follow_frames
            << ",\"feed_frames\":" << result.effective.feed_frames
            << ",\"encoder_extrapolation\":" << (result.effective.encoder_extrapolation?"true":"false")
            << ",\"vertical_axis_enabled\":" << (result.effective.vertical_axis_enabled?"true":"false")
            << "}"
            << ",\"async_stages\":" << (scenarios[i].async_stages?"true":"false")
            << ",\"encoder_delay_ms\":" << scenarios[i].encoder_delay_ms
            << ",\"encoder_drop_rate\":" << scenarios[i].encoder_drop_rate
            << ",\"encoder_max_age_ms\":" << scenarios[i].encoder_max_age_ms
            << ",\"encoder_drop_end_frame\":" << scenarios[i].encoder_drop_end_frame
            << ",\"feedback_prediction\":" << (scenarios[i].feedback_prediction ? "true" : "false")
            << ",\"feedback_drop_rate\":" << scenarios[i].feedback_drop_rate
            << ",\"feedback_timeout_ms\":" << scenarios[i].feedback_timeout_ms
            << ",\"feedback_drop_end_frame\":" << scenarios[i].feedback_drop_end_frame
            << ",\"feedback_resync_frame\":" << scenarios[i].feedback_resync_frame
            << ",\"feedback_transport\":" << (scenarios[i].feedback_transport?"true":"false")
            << ",\"stop_request_delay_ms\":" << scenarios[i].stop_request_delay_ms
            << ",\"feedback_delay_ms\":" << scenarios[i].feedback_delay_ms
            << ",\"feedback_jitter_ms\":" << scenarios[i].feedback_jitter_ms
            << ",\"feedback_capacity\":" << scenarios[i].feedback_capacity
            << ",\"delay_jitter_ms\":" << scenarios[i].delay_jitter_ms
            << ",\"pid_kp\":" << scenarios[i].pid_kp
            << ",\"pid_ki\":" << scenarios[i].pid_ki
            << ",\"pid_kd\":" << scenarios[i].pid_kd
            << ",\"actuator_units_per_px\":" << scenarios[i].actuator_units_per_px
            << ",\"distractor\":" << (scenarios[i].distractor ? "true" : "false")
            << ",\"image_detector\":" << jsonString(scenarios[i].image_detector)
            << ",\"planned_frames\":" << scenarios[i].frames
            << ",\"success_after_frame\":" << scenarios[i].success_after_frame
            << ",\"settle_frames\":" << scenarios[i].settle_frames
            << ",\"epsilon_enter\":" << scenarios[i].epsilon_enter
            << ",\"epsilon_exit\":" << scenarios[i].epsilon_exit
            << ",\"selected_track_id\":" << scenarios[i].selected_track_id
            << ",\"prediction_horizon_s\":" << scenarios[i].command_horizon_s
            << ",\"terminal_frame\":" << result.terminal_frame
            << ",\"metrics\":" << metricJson(result.metrics) << "}";
  }
  int maintained=0,end_stable=0;
  for(const auto& result:results) {maintained+=result.metrics.success_maintained;end_stable+=result.metrics.end_stable;}
  summary << "],\"aggregate\":{\"success_maintained\":" << maintained
          << ",\"end_stable\":" << end_stable << ",\"success\":" << success
          << ",\"failed\":" << failed
          << ",\"unknown\":" << unknown
          << ",\"scenarios\":" << results.size()
          << ",\"success_rate\":"
          << (results.empty() ? 0.0 : static_cast<double>(success) / results.size())
          << ",\"by_motion\":{";
  bool first = true;
  for (const auto& [name, group] : by_motion) {
    if (!first) summary << ",";
    first = false;
    summary << jsonString(name) << ":{\"total\":" << group.total
            << ",\"success\":" << group.success
            << ",\"failed\":" << group.failed
            << ",\"unknown\":" << group.unknown
            << ",\"mae_mean\":" << (group.total ? group.mae_sum / group.total : 0)
            << "}";
  }
  summary << "},\"by_size\":{";
  first = true;
  for (const auto& [name, group] : by_size) {
    if (!first) summary << ",";
    first = false;
    summary << jsonString(name) << ":{\"total\":" << group.total
            << ",\"success\":" << group.success
            << ",\"failed\":" << group.failed
            << ",\"unknown\":" << group.unknown
            << ",\"mae_mean\":" << (group.total ? group.mae_sum / group.total : 0)
            << "}";
  }
  summary << "},\"all_terminal\":" << (all_terminal ? "true" : "false")
          << "}}\n";
  summary.close();

  std::ofstream report(output / "report.md");
  report << "# 自建二维仿真闭环\n\n";
  report << "无游戏/桌面输入；SUCCESS/FAILED/UNKNOWN 来自仿真事件。\n\n";
  report << "结果为首次终结事件的锁存状态；SUCCESS不代表随后持续保持。持续保持要求成功后至少观察settle_frames帧，期间全部可控且误差不超过epsilon_exit。结束稳定要求最后连续settle_frames帧可控且误差严格小于epsilon_enter。\n\n";
  report << "| 场景 | 运动 | 尺寸桶 | 结果 | 原因 | 稳定(s) | MAE(px) | P95(px) | 遮挡恢复(s) | IDSW |\n";
  report << "|---|---|---|---|---|---|---|---|---|---|\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    const auto& metrics = result.metrics;
    report << "| " << result.scene_id << " | " << sim::name(result.motion) << " | "
           << sim::sizeBucket(scenarios[i].target_w, scenarios[i].target_h) << " | "
           << sim::name(result.result) << " | " << sim::name(result.cause) << " | "
           << metrics.settling_s << " | " << metrics.mae_px << " | " << metrics.p95_px
           << " | " << metrics.occlusion_recovery_s << " | " << metrics.id_switches
           << " |\n";
  }
  report << "\nsuccess=" << success << " failed=" << failed << " unknown=" << unknown
         << " / " << results.size() << "\n";
  report << "\nsuccess_maintained=" << maintained << " end_stable=" << end_stable << "\n\n";
  report << "| 场景 | 成功后持续保持 | 结束稳定 | 成功后违规帧数 | 首次违规帧 |\n|---|---|---|---|---|\n";
  for(const auto& r:results) report << "| " << r.scene_id << " | " << r.metrics.success_maintained
      << " | " << r.metrics.end_stable << " | " << r.metrics.post_success_violations
      << " | " << r.metrics.first_post_success_violation << " |\n";
  report.close();

  std::cout << "scenarios=" << results.size() << " success=" << success
            << " failed=" << failed << " unknown=" << unknown << "\n";
  return 0;
} catch (const std::exception& exception) {
  std::cerr << exception.what() << "\n";
  return 1;
}
