#include <opencv2/imgproc.hpp>
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
  constant.vx = 0;
  constant.noise_px = 0;
  const auto success = Simulator(constant, tracker).run();
  require(success.result == TaskResult::Success, "constant should succeed");
  require(success.metrics.settling_s >= 0, "success needs settling time");
  require(success.metrics.mae_px > 0, "MAE must be measured");
  require(success.metrics.frames == constant.frames, "success must not truncate scoring window");
  double fullError=0;
  for(const auto& frame:success.frames) fullError+=frame.error_px;
  require(std::abs(fullError/constant.frames-success.metrics.mae_px)<1e-6,"score all frames including startup");
  require(success.metrics.track_coverage<1 && success.metrics.track_coverage>0,"report track coverage");
  require(normalizedError(10,0,12,24)>.8f,"small target must reject ten pixel error");
  require(normalizedError(10,0,48,96)<normalizedError(10,0,12,24),"relative target scale");

  auto moving = constant;
  moving.start_x=200; moving.vx=80; moving.frames=60;
  moving.actuator_max_speed=10000;
  auto uncompensated=Simulator(moving,tracker).run();
  moving.compensate_actuator_delay=true;
  auto compensated=Simulator(moving,tracker).run();
  double previous=0, corrected=0;
  for(size_t i=25;i<compensated.frames.size();++i) {
    previous+=uncompensated.frames[i].error_px;
    corrected+=compensated.frames[i].error_px;
  }
  require(corrected<previous*.5,"delay compensation improves steady motion without relaxing thresholds");
  require(compensated.result==TaskResult::Success,"noise-free delayed motion must settle");
  for(int i=0;i<moving.actuator_delay_frames;++i)
    require(compensated.frames[i].aim_x==moving.width*.5f,"actuator cannot respond before delay");
  auto acquisition=constant;
  acquisition.acquire_single_confirmed=true;
  acquisition.low_confidence_rate=.5f;
  auto byteConfig=tracker; byteConfig.byteTrack=true;
  auto acquired=Simulator(acquisition,byteConfig).run();
  require(acquired.metrics.track_coverage>0,"wait for an actual confirmed ID");
  std::uint64_t chosen=0;
  for(auto& frame:acquired.frames) if(frame.has_track) {
    if(!chosen) chosen=frame.track_id;
    require(frame.track_id==chosen,"acquisition must not switch established ID");
  }
  auto incremental=constant;
  incremental.incremental_control=true;
  incremental.frames=200;
  incremental.delay_jitter_ms=10;
  auto inc=Simulator(incremental,tracker).run();
  auto inc2=Simulator(incremental,tracker).run();
  require(inc.frames.back().error_px<3,"incremental stationary convergence");
  require(inc.metrics.mae_px==inc2.metrics.mae_px,"jitter must be reproducible");
  require(inc.frames.front().aim_x==incremental.width*.5f,"no immediate actuation");
  auto feedback=incremental;
  feedback.view_feedback=true;feedback.pid_kp=2;
  auto fb=Simulator(feedback,tracker).run();
  bool changed=false;
  for(const auto& r:fb.frames) {
    require(r.aim_x==feedback.width*.5f && r.aim_y==feedback.height*.5f,"view feedback keeps image reference centered");
    require(std::abs(r.truth_x+r.camera_x-feedback.start_x)<.001,"action changes next observation projection");
    if(std::abs(r.camera_x)>0) changed=true;
  }
  require(changed,"feedback must affect the camera");
  feedback.encoder_compensation=true;
  feedback.pid_kp=8;
  auto encoded=Simulator(feedback,tracker).run();
  require(encoded.metrics.track_coverage>.95,"applied encoder feedback preserves stationary target identity");
  require(encoded.frames.back().error_px<3,"encoder feedback converges stationary target");
  sim::ImageObservation pixels;
  pixels.sprite=cv::Mat(40,20,CV_8UC3,cv::Scalar(0,255,0));
  pixels.background=cv::Mat(480,640,CV_8UC3,cv::Scalar(0,0,0));
  int image_calls=0;
  pixels.detect=[&](const cv::Mat& image,int frame,std::int64_t) {
    require(frame==image_calls++,"one image observation per frame");
    cv::Mat mask;cv::inRange(image,cv::Scalar(0,200,0),cv::Scalar(0,255,0),mask);
    std::vector<cv::Point> points;cv::findNonZero(mask,points);
    std::vector<mot::Detection> boxes;
    if(!points.empty()) boxes.push_back({1,.99f,cv::Rect2f(cv::boundingRect(points))});
    return boxes;
  };
  auto image_result=Simulator(feedback,tracker,pixels).run();
  require(image_calls==feedback.frames,"all frames use rendered observation");
  require(image_result.frames.back().error_px<3,"pixel-derived observations close the loop");
  cv::Mat context(100,100,CV_8UC3,cv::Scalar(0,0,0));
  cv::rectangle(context,cv::Rect(30,20,20,40),cv::Scalar(0,255,0),cv::FILLED);
  pixels.sprite=context;pixels.sprite_target={30,20,20,40};image_calls=0;
  auto context_result=Simulator(feedback,tracker,pixels).run();
  require(context_result.frames.back().error_px<3,"context region must not change target truth anchor");
  pixels.detect=[](const cv::Mat&,int,std::int64_t){return std::vector<mot::Detection>{};};
  auto empty_images=Simulator(feedback,tracker,pixels).run();
  require(empty_images.metrics.tracked_frames==0,"image backend cannot fall back to truth boxes");
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
  require(unknown_result.frames.size()==unknown.frames,"unknown keeps complete scoring window");
  require(unknown_result.metrics.mae_px>0 && unknown_result.metrics.track_coverage==0,"missing tracks cannot produce zero control error");
  auto missingId=constant;
  missingId.selected_track_id=999;
  const auto unselected=Simulator(missingId,tracker).run();
  require(unselected.metrics.tracked_frames==0,"selection cannot use truth to find another ID");
  auto removed=constant;
  removed.acquire_single_confirmed=true;
  removed.frames=100; removed.occlusion_start=10; removed.occlusion_frames=30;
  const auto noReselect=Simulator(removed,tracker).run();
  for(const auto& frame:noReselect.frames)
    if(frame.frame>=40) require(!frame.has_track,"expired ID cannot silently reselect newborn track");

  auto failure = base();
  failure.scene_id = "control_fail";
  failure.frames = 100;
  failure.start_x = 480;
  failure.vx = 0; failure.noise_px = 0;
  failure.actuator_max_speed = 1;
  failure.seed = 11;
  const auto failed = Simulator(failure, tracker).run();
  require(failed.result == TaskResult::Failed, "slow actuator must time out");
  require(failed.cause == Cause::Control, "slow actuator cause must be control");

  auto occlusion = base();
  occlusion.noise_px = 0; occlusion.vx=0;
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
  require(turned.frames.size()==turn.frames && std::isfinite(turned.metrics.prediction_mae_px), "fixed maneuver window and finite prediction score");

  auto squat = base();
  squat.scene_id = "squat";
  squat.motion = Motion::Squat;
  squat.success_after_frame = 70;
  const auto squatted = Simulator(squat, tracker).run();
  require(squatted.frames.size()==squat.frames && std::isfinite(squatted.metrics.prediction_mae_px), "fixed maneuver window and finite prediction score");

  auto pan = base();
  pan.scene_id = "camera_pan";
  pan.motion = Motion::CameraPan;
  pan.start_x = 400;
  pan.camera_vx = 20;
  pan.frames = 140;
  pan.success_after_frame = 30;
  const auto panned = Simulator(pan, tracker).run();
  require(panned.frames.size()==pan.frames && std::isfinite(panned.metrics.prediction_mae_px), "fixed maneuver window and finite prediction score");

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
