#include "vision/sim/Simulator.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/persistence.hpp>

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
    config.observation_capacity=intOr(node,"observation_capacity",config.observation_capacity);
    config.observation_jitter_ms=doubleOr(node,"observation_jitter_ms",config.observation_jitter_ms);
    config.inference_service_ms=doubleOr(node,"inference_service_ms",config.inference_service_ms);
    config.observation_delay_ms=doubleOr(node,"observation_delay_ms",config.observation_delay_ms);
    config.delay_jitter_ms=doubleOr(node,"delay_jitter_ms",config.delay_jitter_ms);
    config.seed = static_cast<std::uint32_t>(intOr(node, "seed", 1));
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
      << ",\"feedback_pending\":" << frame.feedback_pending
      << ",\"feedback_fault\":" << (frame.feedback_fault?"true":"false")
      << ",\"camera_x\":" << frame.camera_x
      << ",\"camera_y\":" << frame.camera_y
      << ",\"truth_x\":" << frame.truth_x
      << ",\"truth_y\":" << frame.truth_y
      << ",\"occluded\":" << (frame.occluded ? "true" : "false")
      << ",\"observed\":" << (frame.observed ? "true" : "false")
      << ",\"has_track\":" << (frame.has_track ? "true" : "false")
      << ",\"track_id\":" << frame.track_id
      << ",\"control_gate\":" << jsonString(frame.control_gate)
      << ",\"track_state\":" << jsonString(frame.track_state)
      << ",\"ambiguous\":" << (frame.ambiguous ? "true" : "false")
      << ",\"est_x\":" << frame.est_x
      << ",\"est_y\":" << frame.est_y
      << ",\"pred_x\":" << frame.pred_x
      << ",\"pred_y\":" << frame.pred_y
      << ",\"aim_x\":" << frame.aim_x
      << ",\"aim_y\":" << frame.aim_y
      << ",\"error_px\":" << frame.error_px
      << ",\"error_norm\":" << frame.error_norm
      << ",\"age_ms\":" << frame.age_ms
      << ",\"covariance_trace\":" << frame.covariance_trace
      << ",\"detections\":" << frame.detections
      << ",\"false_positives\":" << frame.false_positives;
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
  std::vector<sim::ScenarioResult> results;
  std::map<std::string, Group> by_motion;
  std::map<std::string, Group> by_size;
  int success = 0;
  int failed = 0;
  int unknown = 0;
  bool all_terminal = true;

  for (const auto& scenario : scenarios) {
    sim::Simulator simulator(scenario, tracker_config);
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
  summary << "{\"schema_version\":2,\"evaluation_window\":\"full_scenario\",\"scenarios\":[";
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
            << ",\"feedback_prediction\":" << (scenarios[i].feedback_prediction ? "true" : "false")
            << ",\"feedback_drop_rate\":" << scenarios[i].feedback_drop_rate
            << ",\"feedback_timeout_ms\":" << scenarios[i].feedback_timeout_ms
            << ",\"delay_jitter_ms\":" << scenarios[i].delay_jitter_ms
            << ",\"pid_kp\":" << scenarios[i].pid_kp
            << ",\"pid_ki\":" << scenarios[i].pid_ki
            << ",\"pid_kd\":" << scenarios[i].pid_kd
            << ",\"actuator_units_per_px\":" << scenarios[i].actuator_units_per_px
            << ",\"planned_frames\":" << scenarios[i].frames
            << ",\"selected_track_id\":" << scenarios[i].selected_track_id
            << ",\"prediction_horizon_s\":" << scenarios[i].command_horizon_s
            << ",\"terminal_frame\":" << result.terminal_frame
            << ",\"metrics\":" << metricJson(result.metrics) << "}";
  }
  summary << "],\"aggregate\":{\"success\":" << success
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
  report.close();

  std::cout << "scenarios=" << results.size() << " success=" << success
            << " failed=" << failed << " unknown=" << unknown << "\n";
  return 0;
} catch (const std::exception& exception) {
  std::cerr << exception.what() << "\n";
  return 1;
}
