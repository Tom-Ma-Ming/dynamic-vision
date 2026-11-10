#include "vision/control/Assist.hpp"

#include "vision/io/Sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>

namespace vision::serial {
namespace {

using json = nlohmann::json;

void requireNew(const std::filesystem::path& path) {
  if (std::filesystem::exists(path)) throw std::runtime_error("Output directory must be new: " + path.string());
  std::filesystem::create_directories(path);
}

json profileJson() {
  const auto& profile = developmentProfile();
  const auto& source = sourceBaseline();
  return json{
      {"name", profile.name},
      {"status", "development_baseline_not_device_identification"},
      {"baud", profile.baud},
      {"data_bits", profile.data_bits},
      {"parity", profile.parity},
      {"stop_bits", profile.stop_bits},
      {"host_serial", profile.host_serial},
      {"ack", profile.ack},
      {"sequence", profile.sequence},
      {"stop", profile.stop},
      {"execution_feedback", profile.execution_feedback},
      {"watchdog", profile.watchdog},
      {"supported_motion", profile.supported_motion},
      {"axis_min", profile.axis_min},
      {"axis_max", profile.axis_max},
      {"units", "device_counts"},
      {"line_buffer_chars", profile.line_buffer_chars},
      {"segment_delay_ms", profile.segment_delay_ms},
      {"segment_delay_is_end_to_end", false},
      {"m00_is_stop", false},
      {"source",
       {{"firmware_path", source.firmware_path},
        {"firmware_sha256", source.firmware_sha256},
        {"host_path", source.host_path},
        {"host_sha256", source.host_sha256},
        {"note", source.note}}},
  };
}

json eventJson(const Event& event, const std::string& scene, const std::string& run_id) {
  json row;
  row["scene"] = scene;
  row["run_id"] = run_id;
  row["type"] = event.type;
  row["command_id"] = event.intent.command_id;
  row["epoch"] = event.intent.epoch ? event.intent.epoch : 0;
  row["frame_id"] = event.intent.frame_id;
  row["track_id"] = event.intent.track_id;
  row["generated_ns"] = event.intent.generated_ns;
  row["deadline_ns"] = event.intent.deadline_ns;
  row["time_ns"] = event.time_ns;
  row["controller"] = event.intent.controller;
  row["dx_counts"] = event.intent.dx_counts;
  row["dy_counts"] = event.intent.dy_counts;
  row["error_x_px"] = event.intent.error_x_px;
  row["error_y_px"] = event.intent.error_y_px;
  row["host_terminal"] = event.host == HostTerminal::None ? json(nullptr) : json(hostName(event.host));
  row["device_result"] = deviceName(event.device);
  row["applied_ns"] = event.has_applied_ns ? json(event.applied_ns) : json(nullptr);
  row["execution_source"] = event.execution_source;
  row["bytes_accepted"] = event.bytes_accepted;
  row["intended"] = event.intended;
  row["accepted"] = event.accepted;
  row["physically_sent"] = event.physically_sent;
  row["stop_command"] = event.stop_command;
  row["match"] = event.match;
  row["error"] = event.error;
  row["raw_response"] = event.raw_response;
  row["authorizes_epoch"] = event.authorizes_epoch;
  return row;
}

class RunWriter {
 public:
  explicit RunWriter(const std::filesystem::path& dir, std::int64_t command_cap)
      : dir_(dir), cap_(command_cap) {
    frames_.open(dir / "frames.jsonl");
    commands_.open(dir / "commands.jsonl");
    transport_.open(dir / "transport.jsonl");
    controller_.open(dir / "controller.jsonl");
    for (auto* stream : {&frames_, &commands_, &transport_, &controller_}) {
      stream->exceptions(std::ios::badbit | std::ios::failbit);
    }
  }
  void line(std::ofstream& stream, const json& row, bool critical) {
    const auto begin = std::chrono::steady_clock::now();
    const std::string text = row.dump() + "\n";
    if (critical && static_cast<std::int64_t>(command_bytes_ + text.size()) > cap_) {
      ++dropped_;
      complete_ = false;
    } else {
      stream << text;
      if (&stream == &commands_ || &stream == &transport_) command_bytes_ += static_cast<std::int64_t>(text.size());
    }
    overhead_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
  }
  void frame(const json& row) { line(frames_, row, false); }
  void command(const json& row) { line(commands_, row, true); }
  void transport(const json& row) { line(transport_, row, true); }
  void controller(const json& row) { line(controller_, row, false); }
  void finish(const json& manifest, const json& summary, const std::string& report) {
    frames_.close();
    commands_.close();
    transport_.close();
    controller_.close();
    std::ofstream man(dir_ / "manifest.json");
    std::ofstream sum(dir_ / "summary.json");
    std::ofstream rep(dir_ / "report.md");
    man.exceptions(std::ios::badbit | std::ios::failbit);
    sum.exceptions(std::ios::badbit | std::ios::failbit);
    rep.exceptions(std::ios::badbit | std::ios::failbit);
    man << manifest.dump(2) << "\n";
    sum << summary.dump(2) << "\n";
    rep << report;
  }
  bool complete() const { return complete_; }
  std::uint64_t dropped() const { return dropped_; }
  std::int64_t overhead() const { return overhead_ns_; }

 private:
  std::filesystem::path dir_;
  std::int64_t cap_;
  std::ofstream frames_, commands_, transport_, controller_;
  std::int64_t command_bytes_ = 0;
  std::int64_t overhead_ns_ = 0;
  std::uint64_t dropped_ = 0;
  bool complete_ = true;
};

json hashesFor(const std::vector<std::filesystem::path>& files) {
  json out = json::object();
  for (const auto& file : files) {
    if (file.empty()) continue;
    if (!std::filesystem::exists(file)) out[file.generic_string()] = nullptr;
    else out[file.generic_string()] = sha256File(file);
  }
  return out;
}

std::string trajectorySha(const RunSpec& spec) {
  std::ostringstream text;
  text << std::setprecision(17) << spec.scene_id << '|' << spec.world_x << '|' << spec.world_y << '|'
       << spec.world_vx << '|' << spec.world_vy << '|' << spec.frames << '|' << spec.dt_ns << '|'
       << spec.track_switch_frame;
  for (const auto& sample : spec.human) {
    text << '|' << sample.time_ns << ',' << sample.dx_px << ',' << sample.dy_px << ',' << sample.takeover << ','
         << sample.release;
  }
  return sha256Hex(text.str());
}

void logEvents(RunWriter& writer, const std::vector<Event>& events, const RunSpec& spec,
               std::vector<std::string>& failures) {
  for (const auto& event : events) {
    if (event.stop_command) failures.push_back("stop_command_flag");
    if (event.type == "ack_received" && !spec.backend.profile.ack) failures.push_back("ack_on_noack_profile");
    if (event.authorizes_epoch) failures.push_back("ack_authorized_epoch");
    if (event.device == DeviceResult::Unknown && event.has_applied_ns) failures.push_back("applied_ns_on_unknown");
    if (event.execution_source == "device_verified") failures.push_back("forged_device_verified");
    if (event.host == HostTerminal::WriteCompleted && event.has_applied_ns) failures.push_back("write_filled_applied_ns");
    if (event.host == HostTerminal::WriteCompleted && event.device != DeviceResult::Unknown)
      failures.push_back("write_completed_is_not_device_result");
    if (!event.intended.empty() && !isCanonicalMove(event.intended)) failures.push_back("noncanonical_move");
    const auto row = eventJson(event, spec.scene_id, spec.run_id);
    writer.command(row);
    writer.transport(row);
  }
}

std::string reportFor(const RunSpec& spec, const RunOutcome& outcome) {
  std::ostringstream out;
  out << "# " << spec.scene_id << " / " << controllerName(spec.control.mode) << " / "
      << backendName(spec.backend.kind) << "\n\n";
  out << "总体：" << outcome.overall << "。`summary.passed` 为 false。UNKNOWN 不是通过。\n\n";
  out << "- 协议：legacy_m_noack，115200 8N1，ack/sequence/stop/execution_feedback 均为 false。\n";
  out << "- 主机终态和设备结果分开。无 ACK 时写入完整的设备结果是 UNKNOWN，applied_ns 保持 null。\n";
  out << "- M 0 0 不是 STOP。本轮不发送点击/击发，不打开真实串口，不扫描端口。\n";
  out << "- 人工轨迹 SHA-256：`" << outcome.trajectory_sha256 << "`\n";
  out << "- 末段最大真值误差 px：" << outcome.end_max_truth_error_px << "\n";
  out << "- 资格覆盖：" << outcome.coverage << "\n";
  out << "- 辅助命令数：" << outcome.assist_commands << "，hold 期间新辅助：" << outcome.commands_during_hold
      << "，区外输出：" << outcome.out_of_region << "\n\n";
  out << "## 失败\n\n";
  if (outcome.failures.empty()) out << "无主机硬门槛失败。\n";
  for (const auto& item : outcome.failures) out << "- " << item << "\n";
  out << "\n## UNKNOWN\n\n";
  for (const auto& item : outcome.unknowns) out << "- " << item << "\n";
  out << "\n源码基线不证明当前烧录固件。TensorRT 不在本次进程内执行。\n";
  return out.str();
}

}  // namespace

RunOutcome runOne(const RunSpec& spec, const std::filesystem::path& output) {
  requireNew(output);
  RunWriter writer(output, spec.command_log_cap);
  RunOutcome outcome;
  outcome.trajectory_sha256 = trajectorySha(spec);
  outcome.unknowns.assign(unknownFields().begin(), unknownFields().end());
  outcome.effect_applicable = spec.score_effect && spec.control.mode != ControllerMode::Off;
  std::vector<std::string> failures;
  if (spec.backend.kind == BackendKind::Serial && spec.backend.profile.ack)
    failures.push_back("serial_backend_must_not_enable_ack_by_opening_a_port");

  ExecutionBackend backend(spec.backend);
  AssistController controller(spec.control);
  if (spec.backend.kind == BackendKind::Serial) {
    // The port string is only a refused label. requestOpen does not call open() or scan.
    logEvents(writer, backend.requestOpen("COM3", 0), spec, failures);
  }

  std::mt19937 rng(spec.seed);
  std::normal_distribution<double> noise(0.0, spec.noise_px > 0 ? spec.noise_px : 1.0);
  double world_x = spec.world_x, world_y = spec.world_y;
  double camera_x = spec.camera_x, camera_y = spec.camera_y;
  double previous_camera_x = camera_x, previous_camera_y = camera_y;
  std::size_t human_index = 0;
  std::vector<double> truth_errors;
  int demand = 0, assisted = 0;
  int assist_before_hold = 0;
  bool seen_hold = false;
  bool emit_after_release_window = false;
  std::int64_t release_seen = -1;

  for (int frame = 0; frame < spec.frames; ++frame) {
    const std::int64_t now = static_cast<std::int64_t>(frame) * spec.dt_ns;
    auto ticks = backend.tick(now);
    logEvents(writer, ticks, spec, failures);
    for (const auto& motion : backend.takeApplications()) {
      camera_x += motion.dx_counts / spec.control.counts_per_px;
      camera_y += motion.dy_counts / spec.control.counts_per_px;
    }
    if (frame > 0) {
      const double dt_s = static_cast<double>(spec.dt_ns) * 1e-9;
      world_x += spec.world_vx * dt_s;
      world_y += spec.world_vy * dt_s;
    }
    double human_dx = 0, human_dy = 0;
    bool human_explicit = false, human_release = false;
    while (human_index < spec.human.size() && spec.human[human_index].time_ns <= now) {
      const auto& sample = spec.human[human_index++];
      human_dx += sample.dx_px;
      human_dy += sample.dy_px;
      camera_x += sample.dx_px;
      camera_y += sample.dy_px;
      if (sample.takeover) human_explicit = true;
      if (sample.release) human_release = true;
    }
    const double truth_x = world_x - camera_x;
    const double truth_y = world_y - camera_y;
    const double est_x = truth_x + (spec.noise_px > 0 ? noise(rng) : 0);
    const double est_y = truth_y + (spec.noise_px > 0 ? noise(rng) : 0);
    Observation observation;
    observation.frame_id = static_cast<std::uint64_t>(frame);
    observation.capture_ns = now;
    observation.publish_ns = now;
    observation.control_ns = now;
    observation.track_id = spec.track_switch_frame >= 0 && frame >= spec.track_switch_frame ? 2 : 1;
    observation.has_track = true;
    observation.track_state = "Confirmed";
    observation.ambiguous = false;
    observation.identity_conflict = false;
    observation.age_ms = 0;
    observation.covariance = 0.1;
    observation.confidence = 1;
    observation.point_source = "synthetic_sensor";
    observation.est_x = est_x;
    observation.est_y = est_y;
    observation.human_explicit = human_explicit;
    observation.human_release = human_release;
    observation.human_dx_px = human_dx;
    observation.human_dy_px = human_dy;

    const Decision decision = controller.step(observation);
    if (decision.emit) {
      ++outcome.assist_commands;
      if (!seen_hold) ++assist_before_hold;
      Intent intent;
      intent.run_id = spec.run_id;
      intent.epoch = decision.epoch;
      intent.command_id = decision.command_id;
      intent.frame_id = observation.frame_id;
      intent.track_id = observation.track_id;
      intent.generated_ns = now;
      intent.deadline_ns = decision.deadline_ns;
      intent.error_x_px = decision.error_x;
      intent.error_y_px = decision.error_y;
      intent.dx_counts = decision.dx_counts;
      intent.dy_counts = decision.dy_counts;
      intent.controller = decision.controller;
      intent.controller_version = spec.control.version;
      auto submitted = backend.submit(intent, now);
      auto pumped = backend.pump(now);
      logEvents(writer, submitted, spec, failures);
      logEvents(writer, pumped, spec, failures);
    }
    if (human_explicit || decision.transition == "human_turn") {
      logEvents(writer, backend.takeover(now, human_explicit), spec, failures);
    }
    if (human_release) {
      release_seen = now;
      logEvents(writer, backend.release(now), spec, failures);
    }
    if (decision.human_hold) {
      seen_hold = true;
      if (decision.emit) {
        ++outcome.commands_during_hold;
        failures.push_back("assist_during_hold");
      }
    }
    if (decision.emit && !decision.latched) {
      ++outcome.out_of_region;
      failures.push_back("out_of_region_output");
    }
    const int limit = std::min(spec.control.max_counts_per_axis, spec.control.firmware_axis_limit);
    if (std::abs(decision.dx_counts) > limit || std::abs(decision.dy_counts) > limit)
      failures.push_back("over_limit_counts");
    if (decision.emit && decision.selected_track && observation.track_id != decision.selected_track)
      failures.push_back("identity_switch_output");
    if (release_seen >= 0 && !decision.human_hold && !decision.waiting && decision.emit) emit_after_release_window = true;

    const bool eligible = decision.latched && decision.ready && !decision.human_hold && !decision.waiting &&
                          decision.distance_px > spec.control.deadzone_px;
    if (eligible) {
      ++demand;
      if (decision.emit || decision.gate == "in_flight" || decision.gate == "fraction_pending") ++assisted;
    }
    truth_errors.push_back(std::hypot(truth_x - spec.control.center_x, truth_y - spec.control.center_y));

    json frame_row;
    frame_row["scene"] = spec.scene_id;
    frame_row["run_id"] = spec.run_id;
    frame_row["controller"] = controllerName(spec.control.mode);
    frame_row["backend"] = backendName(spec.backend.kind);
    frame_row["frame"] = frame;
    frame_row["capture_ns"] = now;
    frame_row["publish_ns"] = now;
    frame_row["control_ns"] = now;
    frame_row["media_pts_ns"] = nullptr;
    frame_row["gpu_event_ns"] = nullptr;
    frame_row["device_time_ns"] = nullptr;
    frame_row["image_w"] = spec.control.image_w;
    frame_row["image_h"] = spec.control.image_h;
    frame_row["center_x"] = spec.control.center_x;
    frame_row["center_y"] = spec.control.center_y;
    frame_row["track_id"] = observation.track_id;
    frame_row["track_state"] = observation.track_state;
    frame_row["has_track"] = true;
    frame_row["ambiguous"] = false;
    frame_row["identity_conflict"] = false;
    frame_row["age_ms"] = 0;
    frame_row["covariance"] = observation.covariance;
    frame_row["max_age_ms"] = spec.control.max_age_ms;
    frame_row["covariance_limit"] = spec.control.covariance_limit;
    frame_row["point_source"] = observation.point_source;
    frame_row["confidence"] = 1;
    frame_row["est_x"] = est_x;
    frame_row["est_y"] = est_y;
    frame_row["est_vx"] = 0;
    frame_row["est_vy"] = 0;
    frame_row["truth_x"] = truth_x;
    frame_row["truth_y"] = truth_y;
    frame_row["truth_use"] = "score_only";
    frame_row["human_dx_px"] = human_dx;
    frame_row["human_dy_px"] = human_dy;
    frame_row["human_explicit"] = human_explicit;
    frame_row["human_release"] = human_release;
    frame_row["human_hold"] = decision.human_hold;
    frame_row["assist_dx_counts"] = decision.dx_counts;
    frame_row["assist_dy_counts"] = decision.dy_counts;
    frame_row["assist_dx_px"] = decision.post_x_px;
    frame_row["assist_dy_px"] = decision.post_y_px;
    frame_row["executed_dx_px"] = camera_x - previous_camera_x;
    frame_row["executed_dy_px"] = camera_y - previous_camera_y;
    frame_row["error_px"] = truth_errors.back();
    frame_row["estimate_error_px"] = decision.distance_px;
    frame_row["gate"] = decision.gate;
    frame_row["emit"] = decision.emit;
    frame_row["dt_s"] = decision.dt_valid ? json(decision.dt_s) : json(nullptr);
    frame_row["ready"] = decision.ready;
    frame_row["latched"] = decision.latched;
    frame_row["waiting"] = decision.waiting;
    writer.frame(frame_row);

    json control;
    control["scene"] = spec.scene_id;
    control["run_id"] = spec.run_id;
    control["controller"] = decision.controller;
    control["law"] = decision.law;
    control["frame"] = frame;
    control["control_ns"] = now;
    control["dt_s"] = decision.dt_valid ? json(decision.dt_s) : json(nullptr);
    control["error_x_px"] = decision.error_x;
    control["error_y_px"] = decision.error_y;
    control["p_x"] = decision.p_x;
    control["p_y"] = decision.p_y;
    control["i_x"] = decision.i_x;
    control["i_y"] = decision.i_y;
    control["d_x"] = decision.d_x;
    control["d_y"] = decision.d_y;
    control["horizon_s"] = 0;
    control["gate"] = decision.gate;
    control["transition"] = decision.transition;
    control["pre_limit_x_px"] = decision.pre_x_px;
    control["pre_limit_y_px"] = decision.pre_y_px;
    control["post_limit_x_px"] = decision.post_x_px;
    control["post_limit_y_px"] = decision.post_y_px;
    control["integral_reset"] = decision.integral_reset;
    control["saturated"] = decision.saturated;
    control["human_hold"] = decision.human_hold;
    control["human_release"] = human_release;
    control["frac_x"] = decision.frac_x;
    control["frac_y"] = decision.frac_y;
    control["emit"] = decision.emit;
    control["used_hidden_truth"] = false;
    writer.controller(control);
    previous_camera_x = camera_x;
    previous_camera_y = camera_y;
  }

  const int window = std::min(spec.end_window, static_cast<int>(truth_errors.size()));
  double end_max = 0;
  for (int i = static_cast<int>(truth_errors.size()) - window; i < static_cast<int>(truth_errors.size()); ++i)
    end_max = std::max(end_max, truth_errors[static_cast<std::size_t>(i)]);
  outcome.end_max_truth_error_px = end_max;
  outcome.coverage = demand ? static_cast<double>(assisted) / demand : 0;
  if (spec.role == "hold" && spec.control.mode != ControllerMode::Off && assist_before_hold == 0)
    failures.push_back("hold_scene_produced_no_assist_before_latch");
  if (spec.role == "release" && spec.control.mode != ControllerMode::Off && !emit_after_release_window)
    failures.push_back("release_did_not_resume");
  if (spec.role == "boundary" && outcome.assist_commands != 0) failures.push_back("boundary_produced_assist");
  if (!writer.complete() || writer.dropped()) failures.push_back("critical_log_dropped");
  if (outcome.effect_applicable) {
    outcome.effect_ok = failures.empty() && end_max <= spec.end_error_limit_px && outcome.coverage >= spec.coverage_min &&
                        outcome.commands_during_hold == 0 && outcome.out_of_region == 0;
    if (end_max > spec.end_error_limit_px) failures.push_back("end_error_above_frozen_threshold");
    if (outcome.coverage < spec.coverage_min) failures.push_back("coverage_below_frozen_threshold");
  }
  outcome.failures = failures;
  outcome.hard_ok = std::none_of(failures.begin(), failures.end(), [](const std::string& item) {
    return item != "end_error_above_frozen_threshold" && item != "coverage_below_frozen_threshold";
  });
  if (!outcome.hard_ok || (outcome.effect_applicable && !outcome.effect_ok)) outcome.overall = "failed";
  else outcome.overall = "UNKNOWN";
  outcome.files_complete = writer.complete();

  json manifest = profileJson();
  manifest["role"] = "control_run";
  manifest["scene"] = spec.scene_id;
  manifest["run_role"] = spec.role;
  manifest["commit"] = spec.commit.empty() ? json(nullptr) : json(spec.commit);
  manifest["dirty"] = spec.dirty;
  manifest["patch_summary"] = spec.dirty ? "worktree_dirty" : "";
  manifest["model_sha256"] = nullptr;
  manifest["model_status"] = "MISSING";
  manifest["engine_sha256"] = nullptr;
  manifest["engine_status"] = "MISSING";
  manifest["sdk"] = {{"tensorrt", "not_loaded"}, {"gpu", std::filesystem::exists("/dev/nvidia0") ? "present" : "none"}};
  manifest["device"] = {{"model", nullptr}, {"firmware", nullptr}, {"port", nullptr}, {"opened", false}};
  manifest["protocol"] = profileJson();
  manifest["parameters"] = {{"controller", controllerName(spec.control.mode)},
                             {"default_is_limited_step", true},
                             {"enter_px", spec.control.enter_px},
                             {"exit_px", spec.control.exit_px},
                             {"deadzone_px", spec.control.deadzone_px},
                             {"max_step_px", spec.control.max_step_px},
                             {"max_speed_px_s", spec.control.max_speed_px_s},
                             {"max_accel_px_s2", spec.control.max_accel_px_s2},
                             {"human_turn_px", spec.control.human_turn_px},
                             {"resume_wait_s", spec.control.resume_wait_s},
                             {"counts_per_px", spec.control.counts_per_px},
                             {"calibration_version", "sim-counts-per-px-v1"},
                             {"device_counts_per_px", nullptr},
                             {"max_counts_per_axis", spec.control.max_counts_per_axis},
                             {"firmware_axis_limit", spec.control.firmware_axis_limit},
                             {"max_in_flight", spec.control.max_in_flight},
                             {"kp", spec.control.kp},
                             {"ki", spec.control.ki},
                             {"kd", spec.control.kd},
                             {"integral_limit", spec.control.integral_limit},
                             {"derivative_tau_s", spec.control.derivative_tau_s},
                             {"output_limit_px_s", spec.control.output_limit_px_s},
                             {"command_lifetime_ns", spec.control.command_lifetime_ns},
                             {"center_x", spec.control.center_x},
                             {"center_y", spec.control.center_y},
                             {"image_w", spec.control.image_w},
                             {"image_h", spec.control.image_h},
                             {"max_age_ms", spec.control.max_age_ms},
                             {"covariance_limit", spec.control.covariance_limit},
                             {"gains_autotuned", false},
                             {"backend", backendName(spec.backend.kind)},
                             {"apply_delay_ns", spec.backend.apply_delay_ns},
                             {"ack", spec.backend.profile.ack},
                             {"seed", spec.seed},
                             {"noise_px", spec.noise_px}};
  manifest["calibration_version"] = "sim-counts-per-px-v1";
  manifest["launch_command"] = spec.launch;
  manifest["audit_version"] = "serial-assist-audit-v1";
  manifest["human_trajectory_sha256"] = outcome.trajectory_sha256;
  manifest["clock"] = "injected_sim_ns";
  manifest["clocks_not_subtracted"] = json::array({"media_pts_ns", "gpu_event_ns", "device_time_ns"});
  manifest["log_policy"] = {{"per_frame_fsync", false},
                             {"rotation", "retained_single_file_stop_on_command_cap"},
                             {"command_log_byte_cap", spec.command_log_cap},
                             {"dropped_critical", writer.dropped()},
                             {"complete", writer.complete()},
                             {"write_overhead_ns", writer.overhead()}};
  manifest["config_sha256"] = hashesFor(spec.hash_files);
  manifest["unknown_fields"] = outcome.unknowns;
  manifest["serial_opened"] = false;
  manifest["click_migrated"] = false;

  json summary;
  summary["schema"] = "serial-assist-summary-v1";
  summary["passed"] = false;
  summary["unknown_counted_as_pass"] = false;
  summary["overall"] = outcome.overall;
  summary["hard_ok"] = outcome.hard_ok;
  summary["effect_applicable"] = outcome.effect_applicable;
  summary["effect_ok"] = outcome.effect_ok;
  summary["end_max_truth_error_px"] = outcome.end_max_truth_error_px;
  summary["coverage"] = outcome.coverage;
  summary["assist_commands"] = outcome.assist_commands;
  summary["commands_during_hold"] = outcome.commands_during_hold;
  summary["out_of_region"] = outcome.out_of_region;
  summary["failures"] = outcome.failures;
  summary["unknowns"] = outcome.unknowns;
  summary["device_execution"] = "UNKNOWN";
  summary["tensorrt"] = "UNKNOWN";
  summary["human_trajectory_sha256"] = outcome.trajectory_sha256;
  summary["controller"] = controllerName(spec.control.mode);
  summary["backend"] = backendName(spec.backend.kind);
  summary["scene"] = spec.scene_id;
  summary["role"] = spec.role;
  summary["seed"] = spec.seed;
  writer.finish(manifest, summary, reportFor(spec, outcome));
  return outcome;
}

namespace {

ControlConfig controlFrom(const json& config, ControllerMode mode) {
  ControlConfig control;
  control.mode = mode;
  const auto& assist = config.at("assist");
  const auto& image = config.at("image");
  const auto& pid = config.at("pid");
  const auto& timing = config.at("timing");
  control.enter_px = assist.at("enter_px");
  control.exit_px = assist.at("exit_px");
  control.deadzone_px = assist.at("deadzone_px");
  control.max_step_px = assist.at("max_step_px");
  control.max_speed_px_s = assist.at("max_speed_px_s");
  control.max_accel_px_s2 = assist.at("max_accel_px_s2");
  control.human_turn_px = assist.at("human_turn_px");
  control.resume_wait_s = assist.at("resume_wait_s");
  control.counts_per_px = config.at("calibration").at("counts_per_px");
  control.max_counts_per_axis = assist.at("max_counts_per_axis");
  control.firmware_axis_limit = assist.at("firmware_axis_limit");
  control.max_in_flight = assist.at("max_in_flight");
  control.kp = pid.at("kp");
  control.ki = pid.at("ki");
  control.kd = pid.at("kd");
  control.integral_limit = pid.at("integral_limit_px_s");
  control.derivative_tau_s = pid.at("derivative_tau_s");
  control.output_limit_px_s = pid.at("output_limit_px_s");
  control.command_lifetime_ns = timing.at("command_lifetime_ns");
  control.center_x = image.at("center_x");
  control.center_y = image.at("center_y");
  control.image_w = image.at("width");
  control.image_h = image.at("height");
  return control;
}

std::vector<HumanSample> humansFrom(const json& node) {
  std::vector<HumanSample> samples;
  for (const auto& item : node) {
    HumanSample sample;
    sample.time_ns = item.at("t_ns");
    sample.dx_px = item.value("dx_px", 0.0);
    sample.dy_px = item.value("dy_px", 0.0);
    sample.takeover = item.value("takeover", false);
    sample.release = item.value("release", false);
    samples.push_back(sample);
  }
  return samples;
}

void validateFrozen(const json& config, const std::filesystem::path& root) {
  if (config.value("autotune", false) || config.value("probe_fire", false))
    throw std::runtime_error("autotune and probe_fire are rejected");
  if (config.at("default_controller") != "limited_step")
    throw std::runtime_error("fixed_pid must not replace limited_step as the default");
  const auto profile = json::parse(std::ifstream(root / "configs/serial/legacy_m_noack.json"));
  const auto& compiled = developmentProfile();
  if (profile.at("name") != compiled.name || profile.at("baud") != compiled.baud || profile.at("ack") != false ||
      profile.at("sequence") != false || profile.at("stop") != false || profile.at("execution_feedback") != false ||
      profile.at("supported_motion") != "M" || profile.at("m00_is_stop") != false)
    throw std::runtime_error("Protocol file does not match the compiled legacy_m_noack profile");
  const auto capability = json::parse(std::ifstream(root / "configs/serial/capability-table.json"));
  for (const auto& field : unknownFields()) {
    bool found = false;
    for (const auto& item : capability.at("unknown_fields")) {
      if (item.at("field") == field) found = true;
    }
    if (!found) throw std::runtime_error("Capability table is missing unknown field " + field);
  }
}

}  // namespace

int runSuite(const SuitePaths& paths) {
  const auto config = json::parse(std::ifstream(paths.config));
  validateFrozen(config, paths.root);
  requireNew(paths.output);
  const auto base = controlFrom(config, ControllerMode::LimitedStep);
  const auto timing = config.at("timing");
  const auto acceptance = json::parse(std::ifstream(paths.root / "configs/serial/acceptance-profile.json"));
  const double end_limit = acceptance.at("synthetic_static").at("end_error_px_max");
  const double coverage_min = acceptance.at("synthetic_static").at("eligible_coverage_min");
  const int end_window = acceptance.at("synthetic_static").at("end_window_frames");
  std::vector<std::filesystem::path> hashed = {
      paths.config, paths.root / "configs/serial/legacy_m_noack.json",
      paths.root / "configs/serial/capability-table.json", paths.root / "configs/serial/acceptance-profile.json",
      paths.root / "CMakeLists.txt", paths.root / "cmake/Dependencies.cmake"};
  json rows = json::array();
  bool hard = true;
  bool effect = true;
  std::map<std::string, std::map<std::string, RunOutcome>> grouped;
  json scenes = json::array();
  for (const auto& direction : config.at("directions")) {
    json scene = direction;
    scene["role"] = "effect";
    scene["velocity"] = {0.0, 0.0};
    scene["seeds"] = {11, 22, 33};
    scene["noise_px"] = 0.05;
    scene["modes"] = {"off", "limited_step", "fixed_pid"};
    scene["backend"] = "simulator";
    scene["delay_ms"] = 0;
    scene["human"] = json::array();
    scene["score_effect"] = true;
    scene["group"] = "compare";
    scenes.push_back(scene);
  }
  for (const auto& scene : config.at("scenes")) scenes.push_back(scene);

  for (const auto& scene : scenes) {
    const std::string id = scene.at("id");
    const std::string role = scene.at("role");
    const auto seeds = scene.at("seeds");
    const auto modes = scene.at("modes");
    for (const auto& seed : seeds) {
      for (const auto& mode_name : modes) {
        RunSpec spec;
        spec.scene_id = id;
        spec.role = role;
        spec.control = base;
        spec.control.mode = parseController(mode_name);
        spec.backend.kind = parseBackend(scene.at("backend"));
        spec.backend.profile = developmentProfile();
        spec.backend.apply_delay_ns = static_cast<std::int64_t>(scene.value("delay_ms", 0)) * 1000000;
        spec.backend.min_send_interval_ns = timing.at("min_send_interval_ns");
        spec.backend.fail_all_writes = scene.value("fail_all_writes", false);
        spec.backend.partial_byte_limit = scene.value("partial_byte_limit", -1);
        spec.world_x = scene.at("world")[0];
        spec.world_y = scene.at("world")[1];
        spec.world_vx = scene.at("velocity")[0];
        spec.world_vy = scene.at("velocity")[1];
        spec.frames = scene.value("frames", timing.at("frames").get<int>());
        spec.dt_ns = timing.at("dt_ns");
        spec.seed = seed;
        spec.noise_px = scene.value("noise_px", 0.0);
        spec.track_switch_frame = scene.value("track_switch_frame", -1);
        spec.human = humansFrom(scene.at("human"));
        spec.commit = paths.commit;
        spec.dirty = paths.dirty;
        spec.launch = paths.launch;
        spec.root = paths.root;
        spec.hash_files = hashed;
        spec.score_effect = scene.value("score_effect", false) && spec.backend.apply_delay_ns == 0;
        spec.end_error_limit_px = end_limit;
        spec.coverage_min = coverage_min;
        spec.end_window = end_window;
        spec.run_id = id + "/seed-" + std::to_string(spec.seed) + "/" + std::string(mode_name);
        const auto leaf = paths.output / scene.at("group").get<std::string>() / id /
                          ("seed-" + std::to_string(spec.seed)) / std::string(mode_name);
        const auto outcome = runOne(spec, leaf);
        if (!outcome.hard_ok) hard = false;
        if (outcome.effect_applicable && !outcome.effect_ok) effect = false;
        grouped[id + "#" + std::to_string(spec.seed)][std::string(mode_name)] = outcome;
        rows.push_back({{"path", leaf.generic_string()},
                        {"scene", id},
                        {"seed", spec.seed},
                        {"controller", mode_name},
                        {"backend", backendName(spec.backend.kind)},
                        {"role", role},
                        {"overall", outcome.overall},
                        {"hard_ok", outcome.hard_ok},
                        {"effect_ok", outcome.effect_ok},
                        {"end_max_truth_error_px", outcome.end_max_truth_error_px},
                        {"coverage", outcome.coverage},
                        {"trajectory", outcome.trajectory_sha256},
                        {"failures", outcome.failures}});
      }
    }
  }
  for (const auto& group : grouped) {
    std::string sha;
    for (const auto& item : group.second) {
      if (sha.empty()) sha = item.second.trajectory_sha256;
      else if (sha != item.second.trajectory_sha256) {
        hard = false;
        rows.push_back({{"scene", group.first}, {"failure", "human_trajectory_mismatch"}});
      }
    }
    if (group.second.count("off") && group.second.count("limited_step") && group.second.count("fixed_pid")) {
      const double off = group.second.at("off").end_max_truth_error_px;
      const double step = group.second.at("limited_step").end_max_truth_error_px;
      const double pid = group.second.at("fixed_pid").end_max_truth_error_px;
      const bool scored = group.second.at("limited_step").effect_applicable;
      if (scored && !(off > step + 5.0 && off > pid + 5.0)) {
        hard = false;
        rows.push_back({{"scene", group.first}, {"failure", "comparison_did_not_improve_on_off"}});
      }
    }
  }
  json summary;
  summary["schema"] = "serial-assist-suite-v1";
  summary["passed"] = false;
  summary["unknown_counted_as_pass"] = false;
  summary["overall"] = (!hard || !effect) ? "failed" : "UNKNOWN";
  summary["hard_ok"] = hard;
  summary["effect_ok"] = effect;
  summary["runs"] = rows;
  summary["unknowns"] = unknownFields();
  summary["tensorrt"] = "UNKNOWN";
  summary["device_execution"] = "UNKNOWN";
  summary["default_controller"] = "limited_step";
  std::ofstream suite(paths.output / "suite-summary.json");
  suite << summary.dump(2) << "\n";
  std::ofstream report(paths.output / "report.md");
  report << "# SA02–SA04 套件\n\n总体：" << summary["overall"].get<std::string>()
         << "。这不是设备通过，也不是 TensorRT 通过。`passed` 为 false。\n\n";
  report << "默认控制器是 limited_step。fixed_pid 只在模式被显式写明时运行，增益来自配置，没有自动调参。\n\n";
  report << "| 场景 | 种子 | 控制器 | 后端 | 末段最大误差 px | 覆盖 | 总体 |\n|---|---:|---|---|---:|---:|---|\n";
  for (const auto& row : rows) {
    if (!row.contains("controller")) continue;
    report << "| " << row["scene"].get<std::string>() << " | " << row["seed"] << " | " << row["controller"].get<std::string>()
           << " | " << row["backend"].get<std::string>() << " | " << row["end_max_truth_error_px"] << " | "
           << row["coverage"] << " | " << row["overall"].get<std::string>() << " |\n";
  }
  report << "\n## UNKNOWN\n\n";
  for (const auto& field : unknownFields()) report << "- " << field << "\n";
  report << "\nWindows 上补跑项目 Engine 的命令（本机未执行）：\n\n```\n"
            "cmake --build --preset windows-sdk --target vision_sim_image\n"
            "out\\windows-sdk\\apps\\trt_infer\\Release\\vision_sim_image.exe "
            "artifacts\\engines\\yolo11n-v002-retrain-20261110-fp32\\best-fp32.engine "
            "<sprite> <background> configs\\tracker-body-primary.yml <NEW_DIR> "
            "19 42.4 431.2 674.9 1 1 0 0 0 0 human_led\n```\n\n"
            "同一条命令最后一个词换为 human_led_off 是关闭对照。该入口没有 fixed_pid 参数；"
            "本轮 PID 对照是 serial_assist run 的 CPU 仿真，不能把 vision_sim_image 说成已经跑过。\n";
  return (!hard || !effect) ? 1 : 2;
}

int runInventory(const SuitePaths& paths) {
  validateFrozen(json::parse(std::ifstream(paths.root / "configs/serial/assist.json")), paths.root);
  requireNew(paths.output);
  const bool nvidia = std::filesystem::exists("/dev/nvidia0") || std::filesystem::exists("/dev/nvidiactl");
  json manifest = profileJson();
  manifest["role"] = "inventory";
  manifest["commit"] = paths.commit.empty() ? json(nullptr) : json(paths.commit);
  manifest["dirty"] = paths.dirty;
  manifest["patch_summary"] = paths.dirty ? "worktree_dirty" : "";
  manifest["model_sha256"] = nullptr;
  manifest["model_status"] = "MISSING";
  manifest["engine_sha256"] = nullptr;
  manifest["engine_status"] = "MISSING";
  const std::filesystem::path model = paths.root / "models/yolo11n-coco-320.onnx";
  if (std::filesystem::exists(model)) {
    manifest["coco_onnx_sha256"] = sha256File(model);
    manifest["coco_onnx_project_contract"] = false;
  }
  const std::string project_onnx = "artifacts/runs/yolo11n-v002-retrain-20261110-320/weights/best.onnx";
  const std::string project_engine = "artifacts/engines/yolo11n-v002-retrain-20261110-fp32/best-fp32.engine";
  manifest["project_onnx"] = project_onnx;
  manifest["project_onnx_status"] = std::filesystem::exists(paths.root / project_onnx) ? "present" : "MISSING";
  manifest["project_engine"] = project_engine;
  manifest["project_engine_status"] = std::filesystem::exists(paths.root / project_engine) ? "present" : "MISSING";
  manifest["sdk"] = {{"tensorrt", "not_loaded"}, {"gpu", nvidia ? "present" : "none"}};
  manifest["device"] = {{"model", nullptr},
                        {"firmware", nullptr},
                        {"port", nullptr},
                        {"opened", false},
                        {"source_proves_flashed_firmware", false}};
  manifest["protocol"] = profileJson();
  manifest["parameters"] = json::parse(std::ifstream(paths.root / "configs/serial/assist.json"));
  manifest["calibration_version"] = "sim-counts-per-px-v1";
  manifest["launch_command"] = paths.launch;
  manifest["audit_version"] = "serial-assist-audit-v1";
  manifest["unknown_fields"] = unknownFields();
  manifest["config_sha256"] = hashesFor({paths.root / "configs/serial/legacy_m_noack.json",
                                         paths.root / "configs/serial/capability-table.json",
                                         paths.root / "configs/serial/acceptance-profile.json",
                                         paths.root / "configs/serial/assist.json", paths.root / "CMakeLists.txt",
                                         paths.root / "cmake/Dependencies.cmake"});
  manifest["serial_opened"] = false;
  manifest["click_migrated"] = false;
  manifest["log_policy"] = {{"per_frame_fsync", false}, {"complete", true}, {"dropped_critical", 0}};
  json summary;
  summary["schema"] = "serial-assist-summary-v1";
  summary["role"] = "inventory";
  summary["passed"] = false;
  summary["unknown_counted_as_pass"] = false;
  summary["overall"] = "UNKNOWN";
  summary["hard_ok"] = !paths.dirty && paths.commit.size() == 40;
  summary["unknowns"] = unknownFields();
  summary["device_execution"] = "UNKNOWN";
  summary["tensorrt"] = "UNKNOWN";
  summary["serial_opened"] = false;
  std::ofstream(paths.output / "frames.jsonl").close();
  std::ofstream(paths.output / "commands.jsonl").close();
  std::ofstream(paths.output / "transport.jsonl").close();
  std::ofstream(paths.output / "controller.jsonl").close();
  std::ofstream(paths.output / "manifest.json") << manifest.dump(2) << "\n";
  std::ofstream(paths.output / "summary.json") << summary.dump(2) << "\n";
  std::ofstream report(paths.output / "report.md");
  report << "# SA01 清单\n\n开发协议是 legacy_m_noack。波特率 115200，8N1，无 ACK、无序号、无 STOP、"
            "无执行回传，只发送 M。移动单位是设备 counts，单轴单段 -127..127。这些来自源码哈希，"
            "不证明设备当前烧录版本。\n\n总体 UNKNOWN。未知项没有计为通过。真实串口未打开。\n\n## UNKNOWN\n\n";
  for (const auto& field : unknownFields()) report << "- " << field << "\n";
  if (paths.dirty || paths.commit.size() != 40) return 1;
  return 0;
}

}  // namespace vision::serial
