#include <opencv2/imgproc.hpp>
#include "vision/sim/Simulator.hpp"
#include "vision/sim/StageScheduler.hpp"

#include <algorithm>
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
  require(success.metrics.success_maintained && success.metrics.end_stable,"stationary baseline maintains success");
  auto final_success=constant;final_success.success_after_frame=constant.frames-constant.settle_frames;
  const auto final_only=Simulator(final_success,tracker).run();
  require(final_only.result==TaskResult::Success && final_only.metrics.end_stable &&
          !final_only.metrics.success_maintained && final_only.metrics.post_success_frames==0,
          "final-frame success is not evidence of subsequent maintenance");
  auto later_failure=constant;later_failure.incremental_control=true;
  later_failure.disconnect_start=100;later_failure.disconnect_frames=20;
  const auto regressed=Simulator(later_failure,tracker).run();
  require(regressed.result==TaskResult::Success,"preserve first-event result for comparison");
  require(!regressed.metrics.success_maintained && !regressed.metrics.end_stable &&
          regressed.metrics.first_post_success_violation==100 && regressed.metrics.post_success_violations==20,
          "later loss of control must invalidate maintained success even at zero position error");
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
  auto telemetry=feedback;telemetry.encoder_transport=true;
  const auto zero_telemetry=Simulator(telemetry,tracker).run();
  require(zero_telemetry.metrics.mae_px==encoded.metrics.mae_px,"zero-delay delivered encoder preserves ideal baseline");
  telemetry.encoder_delay_ms=80;
  const auto delayed_encoder=Simulator(telemetry,tracker).run();
  for(const auto& f:delayed_encoder.frames) {
    if(f.encoder_sample_ts_ns>=0) require(f.encoder_sample_ts_ns<=f.ts_ns-80000000,"no future encoder telemetry");
    require(f.encoder_queued<=256,"encoder transport bounded");
  }
  telemetry.encoder_drop_rate=1;
  const auto missing_encoder=Simulator(telemetry,tracker).run();
  require(missing_encoder.commands.empty() && missing_encoder.cause==Cause::Control,"missing encoder cannot use ideal truth to move");
  for(const auto& f:missing_encoder.frames) require(!f.encoder_valid,"all lost encoder remains unavailable");
  auto missing_predicted=telemetry;missing_predicted.encoder_extrapolation=true;missing_predicted.velocity_feedforward=true;
  require(Simulator(missing_predicted,tracker).run().commands.empty(),"prediction cannot bypass missing encoder gate");
  telemetry.encoder_drop_rate=0;telemetry.encoder_max_age_ms=40;
  const auto stale_encoder=Simulator(telemetry,tracker).run();
  require(stale_encoder.commands.empty(),"encoder older than max age cannot drive control");
  telemetry.encoder_max_age_ms=200;telemetry.encoder_drop_rate=1;telemetry.encoder_drop_end_frame=20;
  telemetry.unknown_grace_frames=100;
  const auto recovered_encoder=Simulator(telemetry,tracker).run();
  require(recovered_encoder.frames[22].encoder_valid,"new delivered paired encoder can recover after blackout");
  for(const auto& e:recovered_encoder.commands) if(e.kind=="generated") require(e.frame_id>=22,"no generation before encoder recovery");
  auto clocks=base();
  clocks.frames=120;clocks.start_x=160;clocks.vx=80;clocks.noise_px=0;
  clocks.incremental_control=true;clocks.acquire_single_confirmed=true;
  clocks.view_feedback=true;clocks.encoder_compensation=true;clocks.encoder_transport=true;
  clocks.encoder_extrapolation=true;clocks.velocity_feedforward=true;
  clocks.encoder_delay_ms=80;clocks.pid_kp=2;clocks.command_horizon_s=0;
  clocks.success_after_frame=80;
  const auto delayed_hold=Simulator(clocks,tracker).run();
  require(delayed_hold.result==TaskResult::Success && delayed_hold.metrics.success_maintained && delayed_hold.metrics.end_stable,
          "noise-free delayed constant remains inside the original gate");
  require(delayed_hold.frames.back().track_id==1,"feedforward must not replace the selected id");
  bool saw_command=false;
  for(const auto& frame:delayed_hold.frames) {
    if(frame.encoder_sample_ts_ns>=0) require(frame.encoder_sample_ts_ns<=frame.ts_ns,"control cannot read a future encoder");
    if(frame.frame>=4 && frame.control_valid) {
      require(std::abs(frame.encoder_age_ms-80)<1e-6,"received encoder age equals the transport delay");
      require(std::abs(frame.observation_age_ms-80)<1e-6,"published vision age matches the paired encoder");
      require(std::abs(frame.command_lead_ms-80)<1e-6,"forward command lead is the scheduled actuator flight");
      require(frame.est_vx>40.f,"filter velocity keeps the target rate rather than the residual image rate");
      saw_command=true;
    } else if(!frame.control_valid) {
      require(frame.command_lead_ms<0,"frames without a command do not invent a lead");
    }
  }
  require(saw_command,"delay clocks need a controllable frame");
  clocks.encoder_drop_rate=1;
  require(Simulator(clocks,tracker).run().commands.empty(),"feedforward cannot move without a delivered encoder");
  auto delayed_gain=feedback;
  delayed_gain.encoder_transport=true;
  delayed_gain.encoder_delay_ms=80;
  delayed_gain.pid_kp=8;
  delayed_gain.frames=120;
  delayed_gain.success_after_frame=40;
  delayed_gain.noise_px=0;
  delayed_gain.encoder_delay_compensation=true;
  const auto held_gain=Simulator(delayed_gain,tracker).run();
  require(held_gain.result==TaskResult::Success && held_gain.metrics.success_maintained && held_gain.metrics.end_stable,
          "delayed stationary gain stays inside the original gate");
  require(held_gain.frames.back().error_px<0.48f,"delayed stationary tail stays inside the enter threshold");
  auto bare_gain=delayed_gain;
  bare_gain.encoder_delay_compensation=false;
  const auto bare_delay=Simulator(bare_gain,tracker).run();
  require(!bare_delay.metrics.end_stable && bare_delay.metrics.mae_px>held_gain.metrics.mae_px,
          "turning compensation off restores the delayed high-gain miss");
  require(!ScenarioConfig{}.encoder_delay_compensation,"delay compensation defaults off");
  auto vertical=delayed_gain;
  vertical.encoder_delay_compensation=false;
  vertical.start_x=320; vertical.start_y=120; vertical.vx=0; vertical.vy=0;
  vertical.pid_kp=2; vertical.frames=200; vertical.success_after_frame=50; vertical.noise_px=0;
  const auto vertical_off=Simulator(vertical,tracker).run();
  int vertical_off_commands=0;
  for(const auto& command:vertical_off.commands)
    if(command.kind=="generated" && std::abs(command.dy)>1e-9) ++vertical_off_commands;
  require(vertical_off_commands>0 && vertical_off.metrics.end_stable &&
          !vertical_off.effective.delay_compensation_active &&
          vertical_off.effective.vertical_axis_enabled &&
          vertical_off.effective.gate_uses_predicted_covariance &&
          vertical_off.effective.maneuver_radius==0,
          "compensation off keeps a vertical offset inside the gate");
  vertical.encoder_delay_compensation=true;
  const auto vertical_on=Simulator(vertical,tracker).run();
  int vertical_on_commands=0;
  for(const auto& command:vertical_on.commands)
    if(command.kind=="generated" && std::abs(command.dy)>1e-9) ++vertical_on_commands;
  require(vertical_on_commands>0 && vertical_on.metrics.end_stable &&
          vertical_on.effective.delay_compensation_active &&
          vertical_on.effective.vertical_axis_enabled &&
          vertical_on.effective.gate_uses_predicted_covariance &&
          vertical_on.effective.measurement_sigma==tracker.sigma &&
          vertical_on.effective.acceleration_sigma==tracker.accel &&
          vertical_on.effective.maneuver_radius==tracker.maneuverRadius,
          "opt-in compensation still controls both axes and keeps the configured tracker");
  auto diagonal=vertical;
  diagonal.vx=40; diagonal.vy=30; diagonal.frames=80; diagonal.success_after_frame=40;
  const auto diag=Simulator(diagonal,tracker).run();
  int diagonal_x=0, diagonal_y=0;
  for(const auto& command:diag.commands) if(command.kind=="generated") {
    if(std::abs(command.dx)>1e-9) ++diagonal_x;
    if(std::abs(command.dy)>1e-9) ++diagonal_y;
  }
  require(diagonal_x>0 && diagonal_y>0,"diagonal motion commands both axes");
  auto squat_delay=vertical;
  squat_delay.motion=Motion::Squat;
  squat_delay.start_y=240; squat_delay.vx=0; squat_delay.vy=0;
  squat_delay.frames=90; squat_delay.success_after_frame=40;
  const auto squat_compensated=Simulator(squat_delay,tracker).run();
  int squat_y=0;
  for(const auto& command:squat_compensated.commands)
    if(command.kind=="generated" && std::abs(command.dy)>1e-9) ++squat_y;
  require(squat_compensated.frames.size()==squat_delay.frames && squat_y>0,
          "squat under opt-in compensation still commands the vertical axis");
  auto omitted=vertical;
  omitted.encoder_delay_compensation=ScenarioConfig{}.encoder_delay_compensation;
  const auto omitted_run=Simulator(omitted,tracker).run();
  require(omitted_run.commands.size()==vertical_off.commands.size() &&
          omitted_run.metrics.mae_px==vertical_off.metrics.mae_px,
          "leaving compensation unset matches turning it off");
  {
    ControllerFlightLedger applied, idle;
    applied.issue(1, 0.25, -0.10, 1.0, -0.40);
    idle.issue(1, 0.25, -0.10, 1.0, -0.40);
    require(applied.pending()==idle.pending(),
            "same issued command starts from the same ledger before any device event");
    applied.deliver(1, 0, 0, false);
    idle.deliver(1, 0, 0, false);
    require(applied.pending()==idle.pending(),
            "identical delivered snapshots keep the ledgers equal");
    applied.deliver(1, 0.40, -0.10, false);
    idle.deliver(1, 0.40, -0.10, false);
    require(applied.pending()==idle.pending() && applied.pending().first<0.25,
            "a delivered application changes both ledgers together");
    applied.deliver(1, 1.0, -0.40, false);
    require(applied.pending()!=idle.pending(),
            "hidden execution is visible only after its snapshot is delivered");
    idle.deliver(1, 1.0, -0.40, true);
    require(idle.pending().first==0 && idle.pending().second==0,
            "a delivered terminal snapshot retires the remainder");
  }
  auto causal=clocks;
  causal.encoder_drop_rate=0;
  causal.encoder_delay_compensation=true;
  causal.feedback_transport=true;
  causal.feedback_delay_ms=500;
  causal.feedback_jitter_ms=0;
  causal.feedback_timeout_ms=2000;
  causal.frames=8;
  const auto undelivered=Simulator(causal,tracker).run();
  require(undelivered.frames.back().feedback_delivered==0,"the delayed feedback has not reached the controller");
  bool hidden_apply=false;
  for(const auto& event:undelivered.commands) if(event.kind=="applied") hidden_apply=true;
  require(hidden_apply,"the device executes before the controller is told");
  const double limit=causal.actuator_max_speed*causal.actuator_units_per_px;
  double pending_x=0, pending_y=0;
  int checked_commands=0;
  for(const auto& frame:undelivered.frames) {
    const CommandEvent* command=nullptr;
    for(const auto& event:undelivered.commands)
      if(event.kind=="generated" && event.frame_id==std::uint64_t(frame.frame)) command=&event;
    if(!command) continue;
    const double target_x=std::clamp(double(frame.est_x),0.0,double(causal.width));
    const double target_y=std::clamp(double(frame.est_y),0.0,double(causal.height));
    const double feed_x=frame.est_vx*causal.actuator_units_per_px;
    const double feed_y=frame.est_vy*causal.actuator_units_per_px;
    const double ux=std::clamp(feed_x+causal.pid_kp*((target_x-causal.width*0.5)*causal.actuator_units_per_px-pending_x),-limit,limit);
    const double uy=std::clamp(feed_y+causal.pid_kp*((target_y-causal.height*0.5)*causal.actuator_units_per_px-pending_y),-limit,limit);
    require(std::abs(command->dx-ux*causal.dt_s)<1e-6 && std::abs(command->dy-uy*causal.dt_s)<1e-6,
            "commands follow delivered feedback only while device execution stays hidden");
    pending_x+=command->dx-feed_x*causal.dt_s;
    pending_y+=command->dy-feed_y*causal.dt_s;
    ++checked_commands;
  }
  require(checked_commands>2,"hidden-execution counterexample needs more than one command");
  auto uncertain=causal;
  uncertain.frames=40;
  uncertain.observation_delay_ms=80;
  uncertain.covariance_limit=50;
  uncertain.vx=0;
  uncertain.start_x=200;
  uncertain.velocity_feedforward=false;
  uncertain.encoder_extrapolation=false;
  auto wide=tracker;
  wide.accel=2000;
  const auto gated=Simulator(uncertain,wide).run();
  require(gated.effective.acceleration_sigma==2000 && gated.effective.gate_uses_predicted_covariance,
          "delay compensation records the configured process noise and predicted gate");
  int uncertain_frames=0;
  for(const auto& frame:gated.frames) {
    if(frame.control_gate=="uncertain") {
      require(frame.covariance_trace>uncertain.covariance_limit,"uncertain gate uses the trace it compared");
      ++uncertain_frames;
    }
    if(frame.observation_age_ms>70 && frame.track_state=="Confirmed")
      require(frame.covariance_trace>2*wide.sigma*wide.sigma,
              "control-time variance exceeds the fresh measurement variance");
  }
  require(uncertain_frames>0,"predicted covariance across the observation delay must be able to close the gate");
  {
    StageScheduler bounded(1);
    require(bounded.enqueue(Stage::Capture,0,0),"stage accepts the first event");
    require(!bounded.enqueue(Stage::Capture,1,1),"stage queue stays bounded");
    require(bounded.rejected(Stage::Capture)==1,"rejection is counted");
    StageScheduler blocked(2);
    require(blocked.enqueue(Stage::Inference,0,0),"queue inference ahead of capture");
    require(!blocked.popDue(0),"inference waits until its capture completes");
    require(blocked.enqueue(Stage::Capture,0,0),"queue the matching capture");
    const auto first=blocked.popDue(0);
    const auto second=blocked.popDue(0);
    require(first && first->stage==Stage::Capture && second && second->stage==Stage::Inference,
            "capture precedes inference at the same timestamp");
    StageScheduler tie(4);
    require(tie.enqueue(Stage::Execution,5,3,1) && tie.enqueue(Stage::Execution,5,-1,0),"queue stop and command");
    require(tie.enqueue(Stage::Feedback,5,3) && tie.enqueue(Stage::Capture,5,3),"queue the earlier stages");
    const auto cap=tie.popDue(5);
    const auto fb=tie.popDue(5);
    const auto stop=tie.popDue(5);
    const auto cmd=tie.popDue(5);
    require(cap && cap->stage==Stage::Capture && fb && fb->stage==Stage::Feedback,"feedback follows capture");
    require(stop && stop->frame==-1 && cmd && cmd->frame==3,"stop wins an equal-time execution");
  }
  auto scheduled=feedback;
  scheduled.encoder_transport=true;
  scheduled.observation_delay_ms=50;
  scheduled.async_stages=true;
  scheduled.frames=12;
  scheduled.pid_kp=2;
  scheduled.noise_px=0;
  const auto once=Simulator(scheduled,tracker).run();
  const auto twice=Simulator(scheduled,tracker).run();
  require(!once.stages.empty() && once.stages.size()==twice.stages.size(),"async schedule repeats");
  int capture_at=-1,inference_at=-1;
  bool off_grid=false;
  auto stage_rank=[](const std::string& stage) {
    if(stage=="capture") return 0;
    if(stage=="inference") return 1;
    if(stage=="feedback") return 2;
    return 3;
  };
  for(size_t i=0;i<once.stages.size();++i) {
    require(once.stages[i].stage==twice.stages[i].stage && once.stages[i].time_ns==twice.stages[i].time_ns,
            "async clocks repeat");
    if(i>0) require(once.stages[i].time_ns>once.stages[i-1].time_ns ||
                    (once.stages[i].time_ns==once.stages[i-1].time_ns &&
                     stage_rank(once.stages[i].stage)>=stage_rank(once.stages[i-1].stage)),
                    "stages follow time, then capture, inference, feedback, execution");
    if(once.stages[i].frame==0 && once.stages[i].stage=="capture") capture_at=int(i);
    if(once.stages[i].frame==0 && once.stages[i].stage=="inference") inference_at=int(i);
    if(once.stages[i].stage=="inference" && once.stages[i].time_ns==50000000) off_grid=true;
  }
  require(capture_at>=0 && inference_at>capture_at,"capture precedes inference of the same frame");
  require(off_grid,"inference keeps its ready time off the frame grid");
  require(!once.observations.empty() && once.observations[0].released==50000000,
          "publish uses the inference clock");
  scheduled.encoder_delay_ms=80;
  scheduled.encoder_drop_rate=1;
  require(Simulator(scheduled,tracker).run().commands.empty(),"async execution stays gated without an encoder");
  scheduled.encoder_drop_rate=0;
  scheduled.encoder_delay_ms=0;
  scheduled.observation_capacity=1;
  scheduled.observation_delay_ms=200;
  scheduled.frames=4;
  require(Simulator(scheduled,tracker).run().stage_rejected>0,"a full inference queue rejects instead of growing");
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
  feedback.subpixel_render=true;feedback.actuator_units_per_px=4;image_calls=0;
  auto subpixel_result=Simulator(feedback,tracker,pixels).run();
  require(subpixel_result.frames.back().error_px<3,"subpixel context renderer maintains body anchor");
  pixels.detect=[](const cv::Mat&,int,std::int64_t){return std::vector<mot::Detection>{};};
  auto empty_images=Simulator(feedback,tracker,pixels).run();
  require(empty_images.metrics.tracked_frames==0,"image backend cannot fall back to truth boxes");
  auto delayed=constant;delayed.observation_delay_ms=80;
  auto delayed_result=Simulator(delayed,tracker).run();
  require(delayed_result.frames[0].observation_frame==-1 && delayed_result.frames[1].observation_frame==-1,"observations cannot publish early");
  for(size_t i=2;i<delayed_result.frames.size();++i) {
    require(delayed_result.frames[i].observation_frame==int(i)-2,"capture identity preserved across delay");
    require(delayed_result.frames[i].observation_ts_ns==std::llround((i-2)*delayed.dt_s*1e9),"capture timestamp must not be rewritten as ready time");
  }
  auto expired=delayed;expired.max_age_ms=40;expired.incremental_control=true;
  auto stale_result=Simulator(expired,tracker).run();
  require(stale_result.commands.empty(),"stale observations cannot create incremental commands");
  for(const auto& f:stale_result.frames) require(f.aim_x==expired.width*.5f,"stale observations cannot move actuator");
  auto reordered=feedback;reordered.observation_delay_ms=80;reordered.observation_jitter_ms=80;
  auto reordered_result=Simulator(reordered,tracker).run();
  int late=0;std::int64_t last_capture=-1;
  for(const auto& e:reordered_result.observations) {
    if(e.status=="stale") ++late;
    if(e.status=="published") require(e.released>=e.ready && e.ready>=e.captured,"publication causality");
  }
  for(const auto& f:reordered_result.frames) {require(f.observation_ts_ns>=last_capture,"late results cannot rewind state");last_capture=f.observation_ts_ns;}
  require(late>0,"jitter fixture exercises stale delivery");
  auto overload=constant;overload.inference_service_ms=100;overload.observation_capacity=2;
  auto overloaded=Simulator(overload,tracker).run();int overflow=0,pending=0;
  for(const auto& e:overloaded.observations) {overflow+=e.status=="overflow";pending+=e.status=="pending";}
  require(overflow>0 && pending<=2,"bounded result buffer under service overload");
  auto disconnected=incremental;disconnected.disconnect_start=10;disconnected.disconnect_frames=20;
  disconnected.success_after_frame=40;
  auto recovered=Simulator(disconnected,tracker).run();
  for(int i=11;i<30;++i) require(recovered.frames[i].aim_x==recovered.frames[10].aim_x,"disconnected actuator must not move");
  for(const auto& e:recovered.commands) {
    if(e.kind=="generated") require(e.frame_id<10 || e.frame_id>=30,"no generation during disconnect");
    if(e.kind=="accepted" && e.timestamp>=1200000000) require(e.generated>=1200000000,"reconnect cannot replay old commands");
  }
  require(recovered.frames.back().error_px<3,"reconnect recovers stationary control");
  auto failed_send=incremental;failed_send.dispatch_failure_rate=1;
  auto send_failure=Simulator(failed_send,tracker).run();
  require(send_failure.result==TaskResult::Unknown && send_failure.cause==Cause::Control,"dispatch failure must be attributed to control");
  for(const auto& f:send_failure.frames) require(f.aim_x==failed_send.width*.5f,"failed sends cannot move");
  auto missing_feedback=incremental;
  missing_feedback.feedback_drop_rate=1;missing_feedback.feedback_timeout_ms=200;
  missing_feedback.success_after_frame=40;
  const auto lost_feedback=Simulator(missing_feedback,tracker).run();
  require(lost_feedback.result==TaskResult::Unknown && lost_feedback.cause==Cause::Control,"feedback timeout is a control fault");
  int fault_frame=-1;float stopped_x=0;
  for(const auto& f:lost_feedback.frames) {
    if(f.feedback_fault && fault_frame<0) {fault_frame=f.frame;stopped_x=f.aim_x;}
    if(fault_frame>=0) {
      require(f.feedback_fault && f.control_gate=="feedback_timeout","feedback fault must latch");
      require(f.aim_x==stopped_x && f.feedback_pending==0,"timeout cancels motion and bounds ledger");
    }
  }
  require(fault_frame>0 && lost_feedback.frames.back().feedback_dropped>0,"loss must be observable");
  bool moved_before_fault=false;
  for(const auto& e:lost_feedback.commands) {
    if(e.kind=="generated") require(e.frame_id<std::uint64_t(fault_frame),"no new commands after timeout");
    if(e.kind=="applied") moved_before_fault=true;
  }
  require(moved_before_fault,"missing notifications do not undo actual execution");
  auto reliable=incremental;reliable.feedback_timeout_ms=500;
  auto reliable_result=Simulator(reliable,tracker).run();
  for(const auto& f:reliable_result.frames) require(!f.feedback_fault,"reliable notifications avoid timeout");
  auto resync=missing_feedback;
  resync.feedback_drop_end_frame=30;resync.feedback_resync_frame=20;
  resync.success_after_frame=50;resync.unknown_grace_frames=100;
  auto resumed=Simulator(resync,tracker).run();
  require(resumed.result==TaskResult::Success,"confirmed idle resync must recover stationary scenario");
  for(int i=6;i<30;++i) require(resumed.frames[i].feedback_fault && !resumed.frames[i].feedback_resynced,"lost stop snapshots cannot unlock control");
  require(resumed.frames[30].feedback_resynced && !resumed.frames[30].feedback_fault,"received stopped snapshot unlocks recovery");
  require(resumed.frames[30].feedback_sync_attempts==11,"recovery requests retry during blackout");
  for(const auto& e:resumed.commands) {
    if(e.kind=="generated") require(e.frame_id<6 || e.frame_id>=30,"blackout cannot generate commands");
    if(e.kind=="accepted" && e.timestamp>=1200000000) require(e.generated>=1200000000,"recovery cannot replay old generation");
  }
  auto no_request=resync;no_request.feedback_resync_frame=-1;
  auto delayed_stop=resync;delayed_stop.feedback_transport=true;
  delayed_stop.feedback_delay_ms=80;delayed_stop.feedback_timeout_ms=200;
  auto delayed_resumed=Simulator(delayed_stop,tracker).run();
  for(int i=6;i<32;++i) require(delayed_resumed.frames[i].feedback_fault,"in-flight stop reply cannot unlock control");
  require(delayed_resumed.frames[32].feedback_resynced,"stop confirmation waits for response delay");
  for(const auto& e:delayed_resumed.commands)
    if(e.kind=="generated") require(e.frame_id<6 || e.frame_id>=32,"no commands before delayed confirmation");
  auto roundtrip_stop=delayed_stop;roundtrip_stop.stop_request_delay_ms=80;
  auto roundtrip=Simulator(roundtrip_stop,tracker).run();
  for(int i=6;i<34;++i) require(roundtrip.frames[i].feedback_fault,"round trip confirmation must not recover early");
  require(roundtrip.frames[34].feedback_resynced,"forward and return delays both count");
  bool residual_motion=false;
  for(const auto& e:roundtrip.commands) {
    if(e.kind=="generated") require(e.frame_id<6 || e.frame_id>=34,"fault immediately blocks generation");
    if(e.kind=="applied" && e.timestamp>240000000 && e.timestamp<=320000000) residual_motion=true;
    if(e.kind=="applied") require(e.timestamp<=320000000 || e.generated>=1360000000,"device stop barrier prevents old movement");
  }
  require(residual_motion,"old command may execute while stop request is in flight");
  roundtrip_stop.feedback_resync_frame=-1;
  auto stopped_only=Simulator(roundtrip_stop,tracker).run();
  require(stopped_only.frames.back().feedback_fault && !stopped_only.frames.back().feedback_resynced,
          "automatic device stop alone cannot authorize recovery");
  for(double forward_ms:{20.,40.}) {
    roundtrip_stop.stop_request_delay_ms=forward_ms;
    auto barrier=Simulator(roundtrip_stop,tracker).run();
    const auto barrier_time=240000000+std::llround(forward_ms*1e6);
    bool pending_cancelled=false;
    for(const auto& e:barrier.commands) {
      if(e.kind=="cancelled_in_transit" && e.timestamp==barrier_time) pending_cancelled=true;
      if(e.kind=="accepted") require(e.timestamp<barrier_time,"stop wins equal-time arrival and invalidates older packets");
      if(e.kind=="applied") require(e.timestamp<=barrier_time,"subframe stop cannot execute until next frame");
    }
    require(pending_cancelled,"stop barrier must cancel pending command at its actual arrival time");
  }
  delayed_stop.feedback_delay_ms=500;
  auto expired_stop=Simulator(delayed_stop,tracker).run();
  require(!expired_stop.frames.back().feedback_resynced && expired_stop.frames.back().feedback_fault,
          "reply beyond timeout cannot unlock control");
  auto held=Simulator(no_request,tracker).run();
  require(held.frames.back().feedback_fault && !held.frames.back().feedback_resynced,"link recovery alone cannot clear fault");
  auto expiry_clock=incremental;expiry_clock.delay_jitter_ms=20;
  expiry_clock.dispatch_failure_rate=.8;expiry_clock.seed=12;
  const auto expiry_result=Simulator(expiry_clock,tracker).run();
  int expiry_events=0;
  for(const auto& e:expiry_result.commands) if(e.kind=="expired") {
    ++expiry_events;
    const auto tick=std::llround(expiry_clock.dt_s*1e9);
    bool report_point=e.timestamp%tick==0;
    for(const auto& arrival:expiry_result.commands)
      if((arrival.kind=="accepted" || arrival.kind=="rejected") && arrival.timestamp==e.timestamp) report_point=true;
    require(report_point && e.timestamp>=e.expires,"expiry notification must use actual advance time");
  }
  require(expiry_events>0,"expiry clock regression must exercise expiration");
  auto transported=incremental;transported.feedback_transport=true;
  transported.feedback_delay_ms=60;transported.feedback_jitter_ms=80;transported.feedback_timeout_ms=1000;
  const auto delayed_feedback=Simulator(transported,tracker).run();
  require(!delayed_feedback.feedback_messages.empty(),"feedback transport must produce evidence");
  for(const auto& f:delayed_feedback.frames) {
    require(!f.feedback_fault && f.feedback_invalid==0,"reordered valid cumulative feedback must not fault");
    require(f.feedback_queued<=std::size_t(transported.feedback_capacity),"feedback queue bounded");
  }
  require(delayed_feedback.frames.back().feedback_ignored>0,"reordering scenario must reject superseded snapshots");
  auto overflow_feedback=transported;overflow_feedback.feedback_capacity=1;overflow_feedback.feedback_delay_ms=300;
  const auto overflow_result=Simulator(overflow_feedback,tracker).run();
  require(overflow_result.frames.back().feedback_overflow>0,"tiny feedback queue must exercise overflow");
  for(const auto& f:overflow_result.frames) require(f.feedback_queued<=1,"overflow queue remains bounded");
  auto epoch_feedback=transported;epoch_feedback.feedback_delay_ms=500;epoch_feedback.feedback_jitter_ms=0;
  epoch_feedback.feedback_timeout_ms=200;epoch_feedback.feedback_resync_frame=20;
  const auto epoch_result=Simulator(epoch_feedback,tracker).run();
  bool stale_epoch=false;
  for(const auto& e:epoch_result.feedback_messages) stale_epoch|=e.status=="stale";
  require(stale_epoch && epoch_result.frames.back().feedback_invalid==0,"late previous epoch feedback is ignored");
  auto capacity_feedback=transported;capacity_feedback.dt_s=.001;capacity_feedback.frames=700;
  capacity_feedback.feedback_delay_ms=1000;capacity_feedback.feedback_jitter_ms=0;capacity_feedback.feedback_timeout_ms=5000;
  const auto capacity_result=Simulator(capacity_feedback,tracker).run();
  require(capacity_result.frames.back().control_gate=="feedback_capacity","ledger capacity fault must be explicit");
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
