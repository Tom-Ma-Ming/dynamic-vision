#pragma once
#include "FeedbackChannel.hpp"

#include <cstdint>
#include <functional>
#include <deque>
#include <string>
#include <vector>

#include "tracker.hpp"

namespace sim {

enum class Motion { Constant, Stop, Turn, Squat, Occlusion, CameraPan };
enum class TaskResult { Running, Success, Failed, Unknown };
enum class Cause { None, Detection, Association, Prediction, Control, Timeout };
// Legacy keeps the existing diagnostic controller. HumanLed is the product
// contract: a person makes large turns, and assist may only trim near center.
enum class AssistMode { Legacy, HumanLed };
enum class AssistState { Disabled, Armed, Assisting, HumanOverride, Suspended };

// External human camera input. Displacements are control-image pixels.
// The controller may read a sample only at or after time_ns.
struct HumanCameraSample {
  std::int64_t time_ns = 0;
  double dx_px = 0;
  double dy_px = 0;
  bool takeover = false;
};

struct ScenarioConfig {
  std::string scene_id = "scene";
  Motion motion = Motion::Constant;
  int frames = 200;
  int selected_track_id = 1;
  bool acquire_single_confirmed = false;
  bool compensate_actuator_delay = false;
  bool incremental_control = false;
  bool view_feedback = false;
  bool subpixel_render = false;
  bool encoder_compensation = false;
  bool encoder_transport = false;
  bool encoder_extrapolation=false,velocity_feedforward=false;
  // Opt-in delay compensation. Off keeps the previous delayed-encoder law.
  bool encoder_delay_compensation=false;
  // Event schedule for capture, inference, feedback, and execution. Default
  // stays on the fixed frame loop so existing runs remain comparable.
  bool async_stages=false;
  double encoder_delay_ms=0,encoder_drop_rate=0,encoder_max_age_ms=200;
  int encoder_drop_end_frame=-1;
  bool feedback_prediction = false;
  double pid_kp=8, pid_ki=0, pid_kd=0;
  double actuator_units_per_px=1;
  double delay_jitter_ms=0;
  double observation_delay_ms=0, observation_jitter_ms=0, inference_service_ms=0;
  int observation_capacity=256;
  int disconnect_start=-1, disconnect_frames=0;
  double dispatch_failure_rate=0;
  double feedback_drop_rate=0, feedback_timeout_ms=0;
  int feedback_drop_end_frame=-1, feedback_resync_frame=-1;
  bool feedback_transport=false;
  double feedback_delay_ms=0,feedback_jitter_ms=0;
  double stop_request_delay_ms=0; // zero preserves the local immediate-stop model
  int feedback_capacity=256;
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
  // Experimental assist limits. They are not a confirmed product standard.
  // Distances are control-image pixels, speed is px/s, acceleration is px/s^2,
  // and human sample times are nanoseconds.
  AssistMode assist_mode = AssistMode::Legacy;
  double assist_enter_px = 48;
  double assist_exit_px = 72;
  double assist_deadzone_px = 1;
  double assist_max_step_px = 2;
  double assist_max_speed_px_s = 50;
  double assist_max_accel_px_s2 = 500;
  double human_turn_px = 12;
  double assist_resume_wait_s = 0.2;
  // Negative center means the image center. HumanLed must set both explicitly.
  double assist_center_x = -1;
  double assist_center_y = -1;
  std::vector<HumanCameraSample> human_camera;
};

struct FrameRecord {
  bool control_valid=false;
  int frame = 0;
  std::int64_t ts_ns = 0;
  std::int64_t observation_ts_ns=-1;
  int observation_frame=-1;
  double estimated_delay_ms=0;
  int feedback_dropped=0;
  bool encoder_valid=true;
  std::int64_t encoder_sample_ts_ns=-1;
  // Ages are -1 when that clock has no delivered sample. command_lead_ms is -1
  // on frames that do not generate a command.
  double observation_age_ms=-1,encoder_age_ms=-1,command_lead_ms=-1;
  int encoder_dropped=0,encoder_overflow=0;
  std::size_t encoder_queued=0;
  bool feedback_fault=false;
  bool feedback_resynced=false;
  int feedback_sync_attempts=0;
  int feedback_delivered=0,feedback_ignored=0,feedback_invalid=0,feedback_overflow=0;
  std::size_t feedback_queued=0;
  std::uint64_t feedback_epoch=0;
  std::size_t feedback_pending=0;
  float camera_x = 0, camera_y = 0;
  float truth_x = 0;
  float truth_y = 0;
  bool occluded = false;
  bool observed = false;
  bool has_track = false;
  bool has_modes = false;
  std::array<double,3> mode_probabilities{};
  std::uint64_t track_id = 0;
  std::string track_state;
  bool ambiguous = false;
  std::string control_gate;
  float est_x = 0;
  float est_y = 0;
  float est_vx = 0;
  float est_vy = 0;
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
  // Assist output is separate from human input and from what the actuator did.
  std::string assist_state = "disabled";
  std::string assist_reason = "legacy";
  bool assist_cancel = false;
  bool human_takeover = false;
  double assist_dx_px = 0, assist_dy_px = 0;
  double human_dx_px = 0, human_dy_px = 0;
  double executed_dx_px = 0, executed_dy_px = 0;
  bool auto_follow = false;
  bool feed_active = false;
  double used_horizon_s = 0;
  std::string gate_transform = "predicted";
};

struct Metrics {
  bool success_maintained=false,end_stable=false;
  int post_success_frames=0,post_success_violations=0,first_post_success_violation=-1,end_stable_frames=0;
  // Full-window actuator error, including frames without an available track.
  double mae_px = 0;
  int tracked_frames = 0;
  int prediction_samples = 0;
  double track_coverage = 0;
  double estimate_mae_px = 0;
  double prediction_mae_px = 0;
  double prediction_coverage = 0;
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

struct CommandEvent {
  std::string kind;
  std::uint64_t id=0, frame_id=0, track_id=0;
  std::int64_t timestamp=0, generated=0, arrival=0, expires=0;
  double dx=0,dy=0;
};

struct ObservationEvent {
  int frame;std::int64_t captured,ready,released=-1;
  double service_ms;std::string status="pending";
};
struct StageTrace {
  std::string stage;
  std::int64_t time_ns=0;
  int frame=0;
};
struct EffectiveControl {
  bool delay_compensation_active = false;
  double measurement_sigma = 0;
  double acceleration_sigma = 0;
  double maneuver_radius = 0;
  double maneuver_acceleration = 0;
  double covariance_limit = 0;
  bool gate_uses_predicted_covariance = true;
  bool velocity_feedforward = false;
  bool encoder_extrapolation = false;
  bool vertical_axis_enabled = true;
  int auto_follow_frames = 0;
  int feed_frames = 0;
};
struct AssistContract {
  std::string mode = "legacy";
  std::string protocol = "legacy";
  bool parameters_experimental = true;
  double center_x = 0, center_y = 0;
  double enter_px = 0, exit_px = 0, deadzone_px = 0;
  double max_step_px = 0, max_speed_px_s = 0, max_accel_px_s2 = 0;
  double human_turn_px = 0, resume_wait_s = 0;
  int human_samples = 0;
  std::string displacement_unit = "control_image_px";
  std::string speed_unit = "px/s";
  std::string acceleration_unit = "px/s^2";
  std::string time_unit = "ns";
};
struct ScenarioResult {
  std::string scene_id;
  Motion motion = Motion::Constant;
  TaskResult result = TaskResult::Running;
  Cause cause = Cause::None;
  int terminal_frame = -1;
  EffectiveControl effective;
  AssistContract assist;
  Metrics metrics;
  std::vector<FrameRecord> frames;
  std::vector<CommandEvent> commands;
  std::vector<ObservationEvent> observations;
  std::vector<FeedbackTrace> feedback_messages;
  std::vector<StageTrace> stages;
  int stage_rejected=0;
};

struct ImageObservation {
  cv::Mat sprite, background;
  std::function<double()> service_time_ms; // optional measured sample after detect
  cv::Rect2f sprite_target; // annotated target inside context image; empty means whole sprite
  std::function<std::vector<mot::Detection>(const cv::Mat&,int,std::int64_t)> detect;
};

class Simulator {
 public:
  Simulator(ScenarioConfig config, mot::Config tracker, ImageObservation images = {});
  ScenarioResult run();

 private:
  ImageObservation images_;
  ScenarioConfig config_;
  mot::Config tracker_;
};

float normalizedError(float dx, float dy, float width, float height);
const char* name(Motion motion);
const char* name(TaskResult result);
const char* name(Cause cause);
std::string sizeBucket(float width, float height);

}  // namespace sim
