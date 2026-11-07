#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "tracker.hpp"

namespace sim {

enum class Motion { Constant, Stop, Turn, Squat, Occlusion, CameraPan };
enum class TaskResult { Running, Success, Failed, Unknown };
enum class Cause { None, Detection, Association, Prediction, Control, Timeout };

struct ScenarioConfig {
  std::string scene_id = "scene";
  Motion motion = Motion::Constant;
  int frames = 200;
  double dt_s = 0.04;
  int width = 640;
  int height = 480;
  float target_w = 24;
  float target_h = 48;
  float start_x = 100;
  float start_y = 240;
  float vx = 120;
  float vy = 0;
  float camera_vx = 0;
  float noise_px = 2;
  float dropout = 0;
  int occlusion_start = -1;
  int occlusion_frames = 0;
  float false_positive_rate = 0;
  float low_confidence_rate = 0;
  float low_confidence_score = 0.2f;
  float epsilon_enter = 0.02f;
  float epsilon_exit = 0.03f;
  int settle_frames = 5;
  int success_after_frame = 0;
  float covariance_limit = 200;
  float max_age_ms = 200;
  float actuator_max_speed = 800;
  int actuator_delay_frames = 2;
  float command_horizon_s = 0.1f;
  int unknown_grace_frames = 50;
  std::uint32_t seed = 1;
};

struct FrameRecord {
  int frame = 0;
  std::int64_t ts_ns = 0;
  float truth_x = 0;
  float truth_y = 0;
  bool occluded = false;
  bool observed = false;
  bool has_track = false;
  std::uint64_t track_id = 0;
  std::string track_state;
  bool ambiguous = false;
  float est_x = 0;
  float est_y = 0;
  float pred_x = 0;
  float pred_y = 0;
  float aim_x = 0;
  float aim_y = 0;
  float error_px = 0;
  float error_norm = 0;
  float age_ms = 0;
  double covariance_trace = 0;
  int detections = 0;
  int false_positives = 0;
};

struct Metrics {
  double mae_px = 0;
  double p95_px = 0;
  double settling_s = -1;
  double overshoot_px = 0;
  double occlusion_recovery_s = -1;
  double age_p50_ms = 0;
  double age_p95_ms = 0;
  double age_p99_ms = 0;
  int frames = 0;
  int detections = 0;
  int false_positives = 0;
  int missed = 0;
  int id_switches = 0;
};

struct ScenarioResult {
  std::string scene_id;
  Motion motion = Motion::Constant;
  TaskResult result = TaskResult::Running;
  Cause cause = Cause::None;
  int terminal_frame = -1;
  Metrics metrics;
  std::vector<FrameRecord> frames;
};

class Simulator {
 public:
  Simulator(ScenarioConfig config, mot::Config tracker);
  ScenarioResult run();

 private:
  ScenarioConfig config_;
  mot::Config tracker_;
};

const char* name(Motion motion);
const char* name(TaskResult result);
const char* name(Cause cause);
std::string sizeBucket(float width, float height);

}  // namespace sim
