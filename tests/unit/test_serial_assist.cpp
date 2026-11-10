#include "vision/control/Assist.hpp"
#include "vision/io/Backend.hpp"
#include "vision/io/Protocol.hpp"
#include "vision/io/Sha256.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

vision::serial::Observation obsAt(std::int64_t t, double x, double y) {
  vision::serial::Observation observation;
  observation.control_ns = t;
  observation.capture_ns = t;
  observation.publish_ns = t;
  observation.has_track = true;
  observation.track_state = "Confirmed";
  observation.track_id = 1;
  observation.confidence = 1;
  observation.covariance = 0.1;
  observation.est_x = x;
  observation.est_y = y;
  observation.point_source = "synthetic_sensor";
  return observation;
}

void testProtocol() {
  using namespace vision::serial;
  require(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256");
  const auto profile = developmentProfile();
  require(profile.baud == 115200 && !profile.ack && !profile.sequence && !profile.stop &&
              !profile.execution_feedback && profile.supported_motion == "M",
          "profile");
  require(!unknownFields().empty(), "unknowns");
  const auto move = encodeMove(3, -2);
  require(move.bytes == "M 3 -2\n" && !move.stop_command, "encode");
  const auto zero = encodeMove(0, 0);
  require(zero.bytes == "M 0 0\n" && !zero.stop_command, "m00");
  int dx = 0, dy = 0;
  require(isCanonicalMove(zero.bytes, &dx, &dy) && dx == 0 && dy == 0, "canonical zero");
  bool rejected = false;
  try { encodeMove(128, 0); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "range");
  require(isForbiddenThisRound("L") && isForbiddenThisRound("CFG") && isForbiddenThisRound("probe_fire") &&
              isForbiddenThisRound("STOP") && !isForbiddenThisRound("M"),
          "forbidden");
}

void testBackends() {
  using namespace vision::serial;
  BackendConfig mock;
  mock.kind = BackendKind::MockSerial;
  mock.profile = developmentProfile();
  ExecutionBackend backend(mock);
  const auto began = std::chrono::steady_clock::now();
  Intent intent;
  intent.command_id = 1;
  intent.epoch = 1;
  intent.dx_counts = 4;
  intent.dy_counts = -1;
  intent.deadline_ns = 1000000000;
  auto queued = backend.submit(intent, 0);
  auto wrote = backend.pump(0);
  require(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(200), "no ack wait");
  require(backend.deviceBuffer() == "M 4 -1\n", "buffer");
  bool completed = false, acked = false, applied = false;
  for (const auto& event : wrote) {
    if (event.host == HostTerminal::WriteCompleted) {
      completed = true;
      require(event.device == DeviceResult::Unknown && !event.has_applied_ns, "unknown device");
    }
    if (event.type == "ack_received") acked = true;
    if (event.has_applied_ns) applied = true;
  }
  require(completed && !acked && !applied && !queued.empty(), "noack write");
  auto ignored = backend.pushAck(1, 10, "OK\n");
  require(ignored.size() == 1 && ignored[0].type == "ack_ignored" && !ignored[0].authorizes_epoch, "ignore ack");

  BackendConfig partial;
  partial.kind = BackendKind::MockSerial;
  partial.partial_byte_limit = 3;
  ExecutionBackend broken(partial);
  intent.command_id = 7;
  broken.submit(intent, 0);
  auto failed = broken.pump(0);
  require(broken.deviceBuffer() == "M 4" && broken.deviceBuffer().find('\n') == std::string::npos, "partial");
  require(broken.faulted(), "fault");
  intent.command_id = 8;
  auto next = broken.submit(intent, 20);
  require(!next.empty() && next.back().host == HostTerminal::Failed, "no next motion");
  require(broken.deviceBuffer() == "M 4", "prefix kept");

  BackendConfig dry;
  dry.kind = BackendKind::DryRun;
  ExecutionBackend dry_backend(dry);
  intent.command_id = 3;
  dry_backend.submit(intent, 0);
  auto recorded = dry_backend.pump(0);
  require(dry_backend.deviceBuffer().empty(), "dry buffer");
  require(recorded.back().host == HostTerminal::DryRunRecorded && !recorded.back().physically_sent &&
              recorded.back().device == DeviceResult::NotSent,
          "dry terminal");

  ExecutionBackend serial(BackendConfig{});
  BackendConfig serial_config;
  serial_config.kind = BackendKind::Serial;
  ExecutionBackend closed(serial_config);
  auto refused = closed.requestOpen("auto", 0);
  require(!closed.opened() && refused[0].type == "open_refused" && refused[0].error == "auto_scan_refused", "scan");
  refused = closed.requestOpen("COM3", 1);
  require(!closed.opened() && refused[0].error == "real_port_not_opened" && closed.deviceBuffer().empty(), "port");
  intent.command_id = 9;
  auto dropped = closed.submit(intent, 2);
  require(dropped.back().host == HostTerminal::Disconnected && !dropped.back().physically_sent, "closed submit");

  BackendConfig ack;
  ack.kind = BackendKind::MockSerial;
  ack.profile.ack = true;
  ack.ack_enabled = true;
  ExecutionBackend ack_backend(ack);
  intent.command_id = 11;
  ack_backend.submit(intent, 0);
  ack_backend.pump(0);
  auto fresh = ack_backend.pushAck(1, 5, "OK\n");
  require(fresh[0].type == "ack_received" && fresh[0].match == "inferred" && !fresh[0].has_applied_ns &&
              !fresh[0].authorizes_epoch && fresh[0].device == DeviceResult::Unknown,
          "ack is not applied");
  ack_backend.setEpoch(2);
  auto stale = ack_backend.pushAck(1, 6, "OK\n");
  require(stale[0].error == "stale_epoch" && !ack_backend.armed() && !stale[0].authorizes_epoch, "stale epoch");

  BackendConfig sim;
  sim.kind = BackendKind::Simulator;
  sim.apply_delay_ns = 50000000;
  ExecutionBackend plant(sim);
  intent.command_id = 12;
  intent.dx_counts = 2;
  intent.dy_counts = 0;
  intent.deadline_ns = 1000000000;
  plant.submit(intent, 1000);
  auto wrote_sim = plant.pump(1000);
  require(wrote_sim.back().host == HostTerminal::WriteCompleted && !wrote_sim.back().has_applied_ns, "sim write");
  require(plant.tick(1000).empty() || plant.takeApplications().empty(), "not yet");
  auto observed = plant.tick(51001000);
  require(!observed.empty() && observed.back().execution_source == "simulator" && observed.back().has_applied_ns &&
              observed.back().applied_ns == 50001000 && observed.back().host == HostTerminal::None,
          "sim apply");
  require(plant.takeApplications().size() == 1, "plant");

  BackendConfig paced;
  paced.kind = BackendKind::MockSerial;
  paced.min_send_interval_ns = 1000000000;
  ExecutionBackend queue(paced);
  intent.command_id = 21;
  intent.dx_counts = 1;
  queue.submit(intent, 0);
  queue.pump(0);
  intent.command_id = 22;
  queue.submit(intent, 10);
  require(queue.pump(10).empty(), "pace");
  auto held = queue.takeover(20, true);
  bool cancelled = false;
  for (const auto& event : held)
    if (event.host == HostTerminal::LocalCancelled && event.intent.command_id == 22) cancelled = true;
  require(cancelled && queue.hold(), "hold cancels unwritten");
  intent.command_id = 23;
  require(queue.submit(intent, 30).back().error == "human_hold", "held submit");
  queue.release(40);
  require(!queue.hold(), "release");
}

void testController() {
  using namespace vision::serial;
  ControlConfig config;
  config.mode = ControllerMode::LimitedStep;
  config.max_in_flight = 0;
  config.max_accel_px_s2 = 100000;
  config.max_step_px = 0.4;
  config.max_counts_per_axis = 2;
  AssistController step(config);
  auto first = step.step(obsAt(0, 340, 240));
  require(!first.emit && !first.dt_valid, "first dt");
  int emitted = 0;
  for (int i = 1; i <= 3; ++i) {
    auto decision = step.step(obsAt(i * 40000000, 340, 240));
    if (decision.emit) {
      require(decision.dx_counts > 0, "sign");
      ++emitted;
    }
  }
  require(emitted == 1, "fraction accumulates");
  step.newEpoch(2);
  auto cleared = step.step(obsAt(4 * 40000000, 340, 240));
  require(!cleared.emit, "epoch clears fraction");

  config.mode = ControllerMode::FixedPid;
  config.max_step_px = 100;
  config.max_counts_per_axis = 127;
  config.kp = 0;
  config.ki = 0;
  config.kd = 1;
  config.derivative_tau_s = 0;
  config.output_limit_px_s = 1000;
  AssistController fast(config);
  AssistController slow(config);
  fast.step(obsAt(0, 340, 240));
  fast.step(obsAt(100000000, 340, 240));
  auto fast_d = fast.step(obsAt(200000000, 350, 240));
  slow.step(obsAt(0, 340, 240));
  slow.step(obsAt(200000000, 340, 240));
  auto slow_d = slow.step(obsAt(400000000, 350, 240));
  require(std::abs(fast_d.d_x) > std::abs(slow_d.d_x), "actual dt");

  AssistController spike(config);
  auto armed = spike.step(obsAt(0, 330, 240));
  auto second = spike.step(obsAt(100000000, 320, 240));
  require(!armed.emit && second.d_x == 0 && second.d_y == 0, "derivative spike guard");

  config.kp = 0;
  config.ki = 10;
  config.kd = 0;
  config.output_limit_px_s = 5;
  AssistController wind(config);
  wind.step(obsAt(0, 420, 240));
  for (int i = 1; i <= 8; ++i) wind.step(obsAt(i * 100000000LL, 420, 240));
  auto unwound = wind.step(obsAt(900000000, 318, 240));
  require(unwound.pre_x_px < 0, "anti-windup");

  config = ControlConfig{};
  config.mode = ControllerMode::Off;
  config.max_in_flight = 0;
  AssistController off(config);
  off.step(obsAt(0, 340, 240));
  require(!off.step(obsAt(40000000, 340, 240)).emit, "off");

  config.mode = ControllerMode::LimitedStep;
  AssistController hold(config);
  hold.step(obsAt(0, 340, 240));
  auto taken = obsAt(40000000, 340, 240);
  taken.human_explicit = true;
  require(!hold.step(taken).emit && hold.step(obsAt(80000000, 340, 240)).human_hold, "latched hold");
  auto released = obsAt(120000000, 340, 240);
  released.human_release = true;
  auto waiting = hold.step(released);
  require(!waiting.human_hold && waiting.waiting && !waiting.emit, "release waits");

  bool backwards = false;
  try { hold.step(obsAt(120000000, 340, 240)); } catch (const std::invalid_argument&) { backwards = true; }
  require(backwards, "clock");
}

void testRunFiles() {
  using namespace vision::serial;
  const auto root = std::filesystem::temp_directory_path() / "serial-assist-unit";
  std::filesystem::remove_all(root);
  RunSpec spec;
  spec.scene_id = "outside";
  spec.role = "boundary";
  spec.control.mode = ControllerMode::LimitedStep;
  spec.backend.kind = BackendKind::Simulator;
  spec.world_x = 120;
  spec.world_y = 240;
  spec.frames = 6;
  spec.commit = std::string(40, 'a');
  spec.launch = "unit";
  auto outside = runOne(spec, root / "outside");
  require(outside.hard_ok && outside.assist_commands == 0 && outside.overall == "UNKNOWN", "outside");
  require(std::filesystem::exists(root / "outside" / "manifest.json") &&
              std::filesystem::exists(root / "outside" / "frames.jsonl") &&
              std::filesystem::exists(root / "outside" / "commands.jsonl") &&
              std::filesystem::exists(root / "outside" / "transport.jsonl") &&
              std::filesystem::exists(root / "outside" / "controller.jsonl") &&
              std::filesystem::exists(root / "outside" / "summary.json") &&
              std::filesystem::exists(root / "outside" / "report.md"),
          "files");
  spec.role = "effect";
  spec.score_effect = false;
  spec.world_x = 340;
  spec.command_log_cap = 40;
  spec.frames = 6;
  auto dropped = runOne(spec, root / "dropped");
  require(!dropped.files_complete && !dropped.hard_ok, "log cap");
  std::filesystem::remove_all(root);
}

}  // namespace

int main() try {
  testProtocol();
  testBackends();
  testController();
  testRunFiles();
  std::cout << "serial assist contract ok\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << "\n";
  return 1;
}
