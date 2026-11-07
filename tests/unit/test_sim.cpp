#include "vision/sim/Simulator.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sim;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

ScenarioConfig base() {
  ScenarioConfig config;
  config.frames = 120;
  config.start_x = 120;
  config.start_y = 240;
  config.vx = 80;
  config.vy = 0;
  config.noise_px = 2;
  config.seed = 1;
  return config;
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 2) throw std::runtime_error("usage: sim_test tracker.yml");
  const mot::Config tracker = mot::Config::load(argv[1]);

  auto constant = base();
  constant.scene_id = "constant";
  const auto success = Simulator(constant, tracker).run();
  require(success.result == TaskResult::Success, "constant should succeed");
  require(success.metrics.settling_s >= 0, "success needs settling time");
  require(success.metrics.mae_px > 0, "MAE must be measured");
  require(success.metrics.p95_px >= success.metrics.mae_px, "P95 >= MAE");

  const auto repeated = Simulator(constant, tracker).run();
  require(repeated.result == success.result, "determinism: result");
  require(repeated.terminal_frame == success.terminal_frame, "determinism: terminal frame");
  require(repeated.metrics.mae_px == success.metrics.mae_px, "determinism: MAE");
  require(repeated.metrics.id_switches == success.metrics.id_switches, "determinism: IDSW");

  auto unknown = base();
  unknown.scene_id = "unknown";
  unknown.frames = 100;
  unknown.dropout = 1;
  unknown.unknown_grace_frames = 20;
  const auto unknown_result = Simulator(unknown, tracker).run();
  require(unknown_result.result == TaskResult::Unknown, "no observations must be UNKNOWN");
  require(unknown_result.cause == Cause::Detection, "unknown cause");

  auto failure = base();
  failure.scene_id = "control_fail";
  failure.frames = 100;
  failure.start_x = 480;
  failure.actuator_max_speed = 1;
  failure.seed = 11;
  const auto failed = Simulator(failure, tracker).run();
  require(failed.result == TaskResult::Failed, "slow actuator must time out");
  require(failed.cause == Cause::Control, "slow actuator cause must be control");

  auto occlusion = base();
  occlusion.scene_id = "occlusion";
  occlusion.frames = 60;
  occlusion.occlusion_start = 5;
  occlusion.occlusion_frames = 10;
  occlusion.success_after_frame = 30;
  const auto occluded = Simulator(occlusion, tracker).run();
  require(occluded.result == TaskResult::Success, "short occlusion should recover");
  require(occluded.metrics.occlusion_recovery_s >= 0, "occlusion recovery measured");

  auto turn = base();
  turn.scene_id = "turn";
  turn.motion = Motion::Turn;
  turn.frames = 140;
  turn.success_after_frame = 90;
  const auto turned = Simulator(turn, tracker).run();
  require(turned.result == TaskResult::Success, "turn should succeed");

  auto squat = base();
  squat.scene_id = "squat";
  squat.motion = Motion::Squat;
  squat.success_after_frame = 70;
  const auto squatted = Simulator(squat, tracker).run();
  require(squatted.result == TaskResult::Success, "squat should succeed");

  auto pan = base();
  pan.scene_id = "camera_pan";
  pan.motion = Motion::CameraPan;
  pan.start_x = 400;
  pan.camera_vx = 20;
  pan.frames = 140;
  pan.success_after_frame = 30;
  const auto panned = Simulator(pan, tracker).run();
  require(panned.result == TaskResult::Success, "camera pan should succeed");

  require(success.result == TaskResult::Success &&
              failed.result == TaskResult::Failed &&
              unknown_result.result == TaskResult::Unknown,
          "T12 needs SUCCESS, FAILED and UNKNOWN");
  std::cout << "Simulation scenarios passed: constant, turn, squat, occlusion, "
               "camera_pan, unknown, control_fail\n";
  return 0;
} catch (const std::exception& exception) {
  std::cerr << exception.what() << "\n";
  return 1;
}
