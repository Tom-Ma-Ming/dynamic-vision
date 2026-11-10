#pragma once

#include "vision/io/Backend.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vision::serial {

enum class ControllerMode { Off, LimitedStep, FixedPid };

const char* controllerName(ControllerMode mode);
ControllerMode parseController(const std::string& name);

struct ControlConfig {
  ControllerMode mode = ControllerMode::LimitedStep;
  double enter_px = 48;
  double exit_px = 72;
  double deadzone_px = 1;
  double max_step_px = 2;
  double max_speed_px_s = 50;
  double max_accel_px_s2 = 500;
  double human_turn_px = 12;
  double resume_wait_s = 0.2;
  double counts_per_px = 1;
  int max_counts_per_axis = 2;
  int firmware_axis_limit = 127;
  int max_in_flight = 1;
  double kp = 6;
  double ki = 0;
  double kd = 0.02;
  double integral_limit = 20;
  double derivative_tau_s = 0.03;
  double output_limit_px_s = 50;
  std::int64_t command_lifetime_ns = 80000000;
  double center_x = 320;
  double center_y = 240;
  int image_w = 640;
  int image_h = 480;
  double max_age_ms = 200;
  double covariance_limit = 200;
  std::string version = "serial-assist-v1";
};

struct HumanSample {
  std::int64_t time_ns = 0;
  double dx_px = 0;
  double dy_px = 0;
  bool takeover = false;
  bool release = false;
};

struct Observation {
  std::uint64_t frame_id = 0;
  std::int64_t capture_ns = 0;
  std::int64_t publish_ns = 0;
  std::int64_t control_ns = 0;
  std::uint64_t track_id = 0;
  bool has_track = false;
  std::string track_state = "None";
  bool ambiguous = false;
  bool identity_conflict = false;
  double age_ms = 0;
  double covariance = 0;
  double confidence = 0;
  std::string point_source = "synthetic_sensor";
  double est_x = 0;
  double est_y = 0;
  double est_vx = 0;
  double est_vy = 0;
  bool human_explicit = false;
  bool human_release = false;
  double human_dx_px = 0;
  double human_dy_px = 0;
};

struct Decision {
  bool emit = false;
  int dx_counts = 0;
  int dy_counts = 0;
  double pre_x_px = 0;
  double pre_y_px = 0;
  double post_x_px = 0;
  double post_y_px = 0;
  double p_x = 0, p_y = 0, i_x = 0, i_y = 0, d_x = 0, d_y = 0;
  double dt_s = 0;
  bool dt_valid = false;
  std::string gate;
  std::string transition;
  bool integral_reset = false;
  bool saturated = false;
  bool human_hold = false;
  bool waiting = false;
  bool latched = false;
  bool ready = false;
  double frac_x = 0;
  double frac_y = 0;
  double error_x = 0;
  double error_y = 0;
  double distance_px = 0;
  std::uint64_t command_id = 0;
  std::uint64_t epoch = 1;
  std::int64_t deadline_ns = 0;
  std::string controller;
  std::string law;
  std::uint64_t selected_track = 0;
};

// Single owner of integral, derivative, latch and command ids.
// The original limited_step law follows the human-led trim in the simulator:
// deadzone, step, speed and acceleration caps, then the same vector clamp.
// fixed_pid is explicit and is not the default.
class AssistController {
 public:
  explicit AssistController(ControlConfig config);
  void setMode(ControllerMode mode);
  const ControlConfig& config() const { return config_; }
  ControllerMode mode() const { return config_.mode; }
  std::uint64_t epoch() const { return epoch_; }
  void newEpoch(std::uint64_t epoch);
  Decision step(const Observation& observation);

 private:
  void resetDynamics();
  Decision finish(Decision decision, const Observation& observation);

  ControlConfig config_;
  std::uint64_t epoch_ = 1;
  std::uint64_t next_id_ = 1;
  std::uint64_t selected_ = 0;
  bool hold_ = false;
  bool latched_ = false;
  bool have_clock_ = false;
  bool derivative_ready_ = false;
  std::int64_t last_ns_ = 0;
  std::int64_t resume_at_ = -1;
  std::int64_t in_flight_until_ = -1;
  double integral_x_ = 0, integral_y_ = 0;
  double filt_x_ = 0, filt_y_ = 0;
  double prev_x_ = 0, prev_y_ = 0;
  double speed_x_ = 0, speed_y_ = 0;
  double frac_x_ = 0, frac_y_ = 0;
};

struct RunSpec {
  std::string scene_id = "scene";
  std::string role = "effect";
  std::string run_id;
  ControlConfig control{};
  BackendConfig backend{};
  double world_x = 340;
  double world_y = 240;
  double world_vx = 0;
  double world_vy = 0;
  double camera_x = 0;
  double camera_y = 0;
  int frames = 80;
  std::int64_t dt_ns = 40000000;
  std::uint32_t seed = 1;
  double noise_px = 0;
  int track_switch_frame = -1;
  std::vector<HumanSample> human;
  std::string commit;
  bool dirty = false;
  std::string launch;
  std::filesystem::path root;
  std::vector<std::filesystem::path> hash_files;
  std::int64_t command_log_cap = 32 * 1024 * 1024;
  std::string trajectory_group;
  bool score_effect = false;
  double end_error_limit_px = 2.5;
  double coverage_min = 0.9;
  int end_window = 25;
};

struct RunOutcome {
  bool files_complete = false;
  bool hard_ok = false;
  bool effect_applicable = false;
  bool effect_ok = false;
  double end_max_truth_error_px = 0;
  double coverage = 0;
  int assist_commands = 0;
  int commands_during_hold = 0;
  int out_of_region = 0;
  std::string trajectory_sha256;
  std::string overall = "UNKNOWN";
  std::vector<std::string> failures;
  std::vector<std::string> unknowns;
};

RunOutcome runOne(const RunSpec& spec, const std::filesystem::path& output);

struct SuitePaths {
  std::filesystem::path root;
  std::filesystem::path config;
  std::filesystem::path output;
  std::string commit;
  bool dirty = false;
  std::string launch;
};

// Writes the SA02 backend matrix and the SA04 same-trajectory comparison.
// Returns 1 when a host hard gate or the frozen synthetic effect check fails.
// Returns 2 when those hold and required device/TRT fields stay UNKNOWN.
// summary.passed is false in both cases. UNKNOWN is not a pass.
int runSuite(const SuitePaths& paths);

int runInventory(const SuitePaths& paths);

}  // namespace vision::serial
