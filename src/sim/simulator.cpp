#include "vision/sim/Simulator.hpp"
#include "vision/sim/Control.hpp"
#include "vision/sim/StopConfirmation.hpp"
#include "vision/sim/EncoderFeedback.hpp"
#include "vision/sim/StageScheduler.hpp"

#include <algorithm>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <deque>
#include <map>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace sim {
namespace {

double percentile(std::vector<double> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  std::size_t rank = static_cast<std::size_t>(std::ceil(p * values.size()));
  if (rank == 0) rank = 1;
  if (rank > values.size()) rank = values.size();
  return values[rank - 1];
}

float clampf(float value, float low, float high) {
  return std::max(low, std::min(high, value));
}

bool validate(const ScenarioConfig& config) {
  if((config.encoder_extrapolation && !config.encoder_transport) ||
     (config.velocity_feedforward && !config.encoder_compensation)) return false;
  auto positive=[&](double value){return std::isfinite(value) && value>0;};
  if(!positive(config.assist_enter_px) || !std::isfinite(config.assist_exit_px) ||
     config.assist_exit_px<config.assist_enter_px) return false;
  if(!std::isfinite(config.assist_deadzone_px) || config.assist_deadzone_px<0 ||
     config.assist_deadzone_px>=config.assist_enter_px) return false;
  if(!positive(config.assist_max_step_px) || !positive(config.assist_max_speed_px_s) ||
     !positive(config.assist_max_accel_px_s2) || !positive(config.human_turn_px) ||
     !std::isfinite(config.assist_resume_wait_s) || config.assist_resume_wait_s<0) return false;
  const bool center_set=config.assist_center_x>=0 || config.assist_center_y>=0;
  if(center_set && !(config.assist_center_x>=0 && config.assist_center_x<=config.width &&
                     config.assist_center_y>=0 && config.assist_center_y<=config.height)) return false;
  if(config.assist_mode==AssistMode::HumanLed && !center_set) return false;
  if(!config.image_detector.empty() && config.image_detector!="pixel") return false;
  if(config.distractor && !config.image_detector.empty()) return false;
  if(config.distractor && !(config.distractor_w>0 && config.distractor_h>0 &&
      std::isfinite(config.distractor_x) && std::isfinite(config.distractor_y) &&
      std::isfinite(config.distractor_vx) && std::isfinite(config.distractor_vy))) return false;
  std::int64_t previous_input=-1;
  for(const auto& sample:config.human_camera) {
    if(sample.time_ns<0 || sample.time_ns<previous_input ||
       !std::isfinite(sample.dx_px) || !std::isfinite(sample.dy_px)) return false;
    previous_input=sample.time_ns;
  }
  if(!std::isfinite(config.encoder_delay_ms) || config.encoder_delay_ms<0 || config.encoder_delay_ms>10000 ||
     !std::isfinite(config.encoder_max_age_ms) || config.encoder_max_age_ms<.001 || config.encoder_max_age_ms>10000 ||
     !std::isfinite(config.encoder_drop_rate) || config.encoder_drop_rate<0 || config.encoder_drop_rate>1 ||
     config.encoder_drop_end_frame<-1 || config.encoder_drop_end_frame>=config.frames ||
     (config.encoder_transport && !config.encoder_compensation)) return false;
  return std::isfinite(config.stop_request_delay_ms) && config.stop_request_delay_ms>=0 && config.stop_request_delay_ms<=10000 && (config.stop_request_delay_ms==0 || config.feedback_transport) && std::isfinite(config.feedback_delay_ms) && config.feedback_delay_ms>=0 && config.feedback_delay_ms<=10000 && std::isfinite(config.feedback_jitter_ms) && config.feedback_jitter_ms>=0 && config.feedback_jitter_ms<=10000 && config.feedback_capacity>=1 && config.feedback_capacity<=10000 && (!config.feedback_transport || (config.incremental_control && config.feedback_timeout_ms>0)) && config.feedback_drop_end_frame>=-1 && config.feedback_drop_end_frame<config.frames && config.feedback_resync_frame>=-1 && config.feedback_resync_frame<config.frames && (config.feedback_resync_frame<0 || config.feedback_timeout_ms>0) && std::isfinite(config.feedback_drop_rate) && config.feedback_drop_rate>=0 && config.feedback_drop_rate<=1 && std::isfinite(config.feedback_timeout_ms) && config.feedback_timeout_ms>=0 && config.feedback_timeout_ms<=10000 && (config.feedback_drop_rate==0 || config.feedback_timeout_ms>0) && (config.feedback_timeout_ms==0 || (config.feedback_timeout_ms>=.001 && config.incremental_control)) && std::isfinite(config.dispatch_failure_rate) && config.dispatch_failure_rate>=0 && config.dispatch_failure_rate<=1 && config.disconnect_start>=-1 && config.disconnect_frames>=0 && (config.disconnect_start<0 || config.disconnect_start<=config.frames-config.disconnect_frames) && ((config.disconnect_frames==0 && config.dispatch_failure_rate==0) || config.incremental_control) && config.observation_capacity>0 && config.observation_capacity<=10000 && std::isfinite(config.observation_jitter_ms) && config.observation_jitter_ms>=0 && config.observation_jitter_ms<=10000 && std::isfinite(config.inference_service_ms) && config.inference_service_ms>=0 && config.inference_service_ms<=10000 && std::isfinite(config.observation_delay_ms) && config.observation_delay_ms>=0 && config.observation_delay_ms<=10000 && (!config.feedback_prediction || config.encoder_compensation) && (!config.encoder_compensation || config.view_feedback) && (!config.view_feedback || config.incremental_control) && std::isfinite(config.actuator_units_per_px) && config.actuator_units_per_px>0 &&
         std::isfinite(config.delay_jitter_ms) && config.delay_jitter_ms>=0 && config.delay_jitter_ms<=1000 &&
         config.selected_track_id > 0 && std::isfinite(config.command_horizon_s) &&
         config.command_horizon_s >= 0 && config.noise_px >= 0 && config.frames > 0 && config.dt_s > 0 && config.width > 0 &&
         config.height > 0 && config.target_w > 0 && config.target_h > 0 &&
         config.target_w <= config.width && config.target_h <= config.height &&
         config.epsilon_enter > 0 && config.epsilon_exit > config.epsilon_enter &&
         config.settle_frames > 0 && config.success_after_frame >= 0 &&
         config.unknown_grace_frames > 0 &&
         config.max_age_ms > 0 && config.covariance_limit > 0 &&
         config.actuator_max_speed > 0 && config.actuator_delay_frames >= 0 &&
         config.dropout >= 0 && config.dropout <= 1 &&
         config.false_positive_rate >= 0 && config.false_positive_rate <= 1 &&
         config.low_confidence_rate >= 0 && config.low_confidence_rate <= 1 &&
         config.low_confidence_score >= 0 && config.low_confidence_score <= 1 &&
         config.occlusion_frames >= 0 &&
         (config.occlusion_start < 0 ||
          config.occlusion_start + config.occlusion_frames <= config.frames);
}

}  // namespace

float normalizedError(float dx, float dy, float width, float height) {
  return std::hypot(dx / width, dy / height);
}

const char* name(Motion motion) {
  switch (motion) {
    case Motion::Constant: return "constant";
    case Motion::Stop: return "stop";
    case Motion::Turn: return "turn";
    case Motion::Squat: return "squat";
    case Motion::Occlusion: return "occlusion";
    case Motion::CameraPan: return "camera_pan";
  }
  return "unknown";
}

const char* name(TaskResult result) {
  switch (result) {
    case TaskResult::Running: return "Running";
    case TaskResult::Success: return "SUCCESS";
    case TaskResult::Failed: return "FAILED";
    case TaskResult::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* name(AssistMode mode) {
  return mode==AssistMode::HumanLed ? "human_led" : "legacy";
}

const char* name(AssistState state) {
  switch(state) {
    case AssistState::Disabled: return "disabled";
    case AssistState::Armed: return "armed";
    case AssistState::Assisting: return "assisting";
    case AssistState::HumanOverride: return "human_override";
    case AssistState::Suspended: return "suspended";
  }
  return "disabled";
}

const char* name(Cause cause) {
  switch (cause) {
    case Cause::None: return "none";
    case Cause::Detection: return "detection";
    case Cause::Association: return "association";
    case Cause::Prediction: return "prediction";
    case Cause::Control: return "control";
    case Cause::Timeout: return "timeout";
  }
  return "unknown";
}

std::string sizeBucket(float width, float height) {
  const float side = std::min(width, height);
  if (side < 16) return "<16";
  if (side < 32) return "16-32";
  if (side < 96) return "32-96";
  return ">96";
}

Simulator::Simulator(ScenarioConfig config, mot::Config tracker, ImageObservation images)
    : images_(std::move(images)), config_(std::move(config)), tracker_(std::move(tracker)) {
  if(images_.detect && (images_.sprite.empty() || images_.background.empty() ||
      images_.sprite.type()!=CV_8UC3 || images_.background.type()!=CV_8UC3))
    throw std::invalid_argument("Image observation requires BGR sprite and background");
  if(config_.image_detector=="pixel" && !images_.detect)
    throw std::invalid_argument("pixel image_detector requires an image observation");
  if(images_.detect) {
    if(images_.sprite_target==cv::Rect2f{}) images_.sprite_target={0,0,float(images_.sprite.cols),float(images_.sprite.rows)};
    const auto b=images_.sprite_target;
    if(!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.width) || !std::isfinite(b.height) ||
       b.width<=0 || b.height<=0 || b.x<0 || b.y<0 || b.br().x>images_.sprite.cols || b.br().y>images_.sprite.rows)
      throw std::invalid_argument("Invalid sprite target region");
  }
  images_.sprite=images_.sprite.clone();images_.background=images_.background.clone();
  if (!validate(config_)) {
    throw std::invalid_argument("Invalid simulation scenario configuration");
  }
}

ScenarioResult Simulator::run() {
  ScenarioResult output;
  output.scene_id = config_.scene_id;
  output.motion = config_.motion;

  const float dt = static_cast<float>(config_.dt_s);
  std::mt19937 rng(config_.seed);
  std::normal_distribution<float> noise(0.0f, config_.noise_px);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);

  auto tracker_config=tracker_;
  if(config_.encoder_compensation) tracker_config.cameraMotion.enabled=true;
  const bool delay_mode=config_.encoder_delay_compensation && config_.encoder_transport &&
      config_.encoder_delay_ms>0 && !config_.feedback_prediction;
  output.effective.delay_compensation_active=delay_mode;
  output.effective.measurement_sigma=tracker_config.sigma;
  output.effective.acceleration_sigma=tracker_config.accel;
  output.effective.maneuver_radius=tracker_config.maneuverRadius;
  output.effective.maneuver_acceleration=tracker_config.maneuverAccel;
  output.effective.covariance_limit=config_.covariance_limit;
  output.effective.gate_uses_predicted_covariance=true;
  output.effective.velocity_feedforward=config_.velocity_feedforward;
  output.effective.encoder_extrapolation=config_.encoder_extrapolation;
  output.effective.vertical_axis_enabled=true;
  output.assist.mode=name(config_.assist_mode);
  output.assist.protocol=config_.assist_mode==AssistMode::HumanLed ? "human-led-v1" : "legacy";
  output.assist.center_x=config_.assist_center_x>=0 ? config_.assist_center_x : config_.width*0.5;
  output.assist.center_y=config_.assist_center_y>=0 ? config_.assist_center_y : config_.height*0.5;
  output.assist.enter_px=config_.assist_enter_px;
  output.assist.exit_px=config_.assist_exit_px;
  output.assist.deadzone_px=config_.assist_deadzone_px;
  output.assist.max_step_px=config_.assist_max_step_px;
  output.assist.max_speed_px_s=config_.assist_max_speed_px_s;
  output.assist.max_accel_px_s2=config_.assist_max_accel_px_s2;
  output.assist.human_turn_px=config_.human_turn_px;
  output.assist.resume_wait_s=config_.assist_resume_wait_s;
  output.assist.human_samples=static_cast<int>(config_.human_camera.size());
  output.assist.enabled=config_.assist_mode==AssistMode::HumanLed && config_.assist_enabled;
  mot::Tracker tracker(tracker_config);
  mot::Selection selection;

  float world_x = config_.start_x;
  float world_y = config_.start_y;
  float distractor_x = config_.distractor_x;
  float distractor_y = config_.distractor_y;
  float vx = config_.vx;
  float vy = config_.vy;
  float height = config_.target_h;
  float bottom = world_y + height * 0.5f;
  float camera_x = 0, camera_y = 0, previous_camera_x = 0, previous_camera_y = 0;
  float aim_x = config_.width * 0.5f;
  float aim_y = config_.height * 0.5f;
  float previous_aim_x = aim_x, previous_aim_y = aim_y;
  AssistState assist_state=AssistState::Disabled;
  bool assist_latched=false;
  bool human_hold=false;
  double assist_speed_x=0, assist_speed_y=0;
  SimTime assist_resume_at=0;
  SimTime assist_known_until=-1;
  std::size_t human_cursor=0;
  float last_est_x = aim_x;
  float last_est_y = aim_y;
  float last_pred_x = aim_x;
  float last_pred_y = aim_y;
  std::deque<std::pair<float, float>> command_queue;
  IncrementActuator actuator(config_.actuator_max_speed*config_.actuator_units_per_px);
  PidAxis pid_x(config_.pid_kp,config_.pid_ki,config_.pid_kd,100,config_.actuator_max_speed*config_.actuator_units_per_px);
  PidAxis pid_y(config_.pid_kp,config_.pid_ki,config_.pid_kd,100,config_.actuator_max_speed*config_.actuator_units_per_px);
  std::vector<IncrementCommand> transit;
  std::mt19937 failure_rng(config_.seed+300003);
  std::uniform_real_distribution<double> failure(0,1);
  std::mt19937 delay_rng(config_.seed+100003);
  std::uniform_real_distribution<double> jitter(-config_.delay_jitter_ms,config_.delay_jitter_ms);
  std::mt19937 observation_rng(config_.seed+200003);
  std::uniform_real_distribution<double> observation_jitter(-config_.observation_jitter_ms,config_.observation_jitter_ms);
  SimTime service_finished=0;
  double encoder_x=0,encoder_y=0,observed_encoder_x=0,observed_encoder_y=0;
  EncoderFeedback encoder_channel(std::llround(config_.encoder_delay_ms*1e6),std::llround(config_.encoder_max_age_ms*1e6));
  StageScheduler stages(static_cast<std::size_t>(std::max(1, config_.observation_capacity)));
  std::mt19937 encoder_rng(config_.seed+700003);
  struct PendingObservation {
    int frame;SimTime captured,ready;std::vector<mot::Detection> detections;
    double encoder_x,encoder_y;
  };
  std::deque<PendingObservation> observations;
  int observation_frame=-1;
  CommandFeedback feedback(config_.actuator_delay_frames*config_.dt_s);
  FeedbackChannel feedback_channel(config_.actuator_delay_frames*config_.dt_s,
      std::llround(config_.feedback_delay_ms*1e6),std::llround(config_.feedback_jitter_ms*1e6),
      config_.feedback_capacity,config_.seed+500003);
  auto feedbackDelay=[&] {return config_.feedback_transport?feedback_channel.delay():feedback.delay();};
  auto feedbackSize=[&] {return config_.feedback_transport?feedback_channel.outstanding():feedback.size();};
  std::mt19937 feedback_rng(config_.seed+400003);
  int feedback_dropped=0;
  bool feedback_fault=false;
  bool feedback_capacity_fault=false;
  bool feedback_resynced=false;
  int feedback_sync_attempts=0;
  StopConfirmation stop_confirmation;
  StopSnapshot stop_snapshot;
  SimTime stop_ready=-1, stop_request_ready=-1;
  std::uint64_t stop_request_nonce=0, stop_request_epoch=0;
  std::mt19937 stop_rng(config_.seed+600003);
  auto dropFeedback=[&](SimTime time) {
    const bool within_window=config_.feedback_drop_end_frame<0 ||
      time<std::llround(config_.feedback_drop_end_frame*config_.dt_s*1e9);
    return within_window && config_.feedback_drop_rate>0 && failure(feedback_rng)<config_.feedback_drop_rate;
  };
  IncrementCommand active_command;
  ControllerFlightLedger flight;
  auto event=[&](const char* kind,const IncrementCommand& c,SimTime time,double dx,double dy) {
    const std::string type(kind);
    const bool remote=type=="accepted" || type=="applied" || type=="expired" || type=="replaced" || type=="rejected";
    const bool dropped=remote && dropFeedback(time);
    if(dropped) ++feedback_dropped;
    if(config_.feedback_transport) feedback_channel.event(type,c,time,dx,dy,dropped);
    else if(dropped) {} // physical event is still recorded below
    else if(type=="generated") feedback.issued(c.id,c.generated,dx,dy);
    else if(type=="accepted") feedback.acknowledged(c.id,time);
    else if(type=="applied") feedback.applied(c.id,dx,dy);
    else feedback.retire(c.id);
    // Same visibility as the controller ledger. A dropped or still-queued device
    // event must not change the in-flight feedback portion.
    if(type!="generated") {
      if(config_.feedback_transport) {
        if(!remote) flight.retire(c.id);
      } else if(!dropped && type=="applied") {
        flight.deliverIncrement(c.id,dx,dy,false);
      } else if(!dropped && type!="accepted") {
        flight.retire(c.id);
      }
    }
    output.commands.push_back({kind,c.id,c.frame_id,c.track_id,time,c.generated,c.arrival,c.expires,dx,dy});
  };
  auto apply=[&](SimTime t) {
    auto delta=actuator.advance(t);
    if(delta.dx!=0 || delta.dy!=0) event("applied",active_command,t,delta.dx,delta.dy);
    if(active_command.id && t>=active_command.expires) {
      // Event timestamp is when advance reports the transition; the physical
      // deadline remains in expires. Never backdate a received notification.
      event("expired",active_command,t,0,0);active_command={};
    }
    if(config_.view_feedback) {
      encoder_x+=delta.dx/config_.actuator_units_per_px;
      encoder_y+=delta.dy/config_.actuator_units_per_px;
      camera_x+=float(delta.dx/config_.actuator_units_per_px);
      camera_y+=float(delta.dy/config_.actuator_units_per_px);
    } else {
      aim_x+=float(delta.dx/config_.actuator_units_per_px);
      aim_y+=float(delta.dy/config_.actuator_units_per_px);
    }
  };


  auto stopAt=[&](SimTime time) {
    apply(time);
    for(const auto& c:transit) event("cancelled_in_transit",c,time,0,0);
    if(active_command.id) event("cancelled",active_command,time,0,0);
    active_command={};transit.clear();actuator.cancel();
  };
  auto replyStop=[&](SimTime time,std::uint64_t epoch,std::uint64_t nonce,bool idle) {
    if(dropFeedback(time)) {++feedback_dropped;return;}
    stop_snapshot={epoch,nonce,time,idle};
    const auto j=std::uniform_int_distribution<SimTime>(-std::llround(config_.feedback_jitter_ms*1e6),
        std::llround(config_.feedback_jitter_ms*1e6))(stop_rng);
    stop_ready=time+std::max<SimTime>(0,std::llround(config_.feedback_delay_ms*1e6)+j);
  };

  std::uint64_t last_track_id = 0;
  int id_switches = 0;
  int settle_count = 0;
  int unseen_frames = 0;
  int confirmed_frames = 0;
  int saturated_frames = 0;
  int missed = 0;
  int total_detections = 0;
  int total_false_positives = 0;
  int occlusion_end =
      config_.occlusion_start >= 0
          ? config_.occlusion_start + config_.occlusion_frames
          : -1;
  bool occlusion_recovered = false;
  int occlusion_recovery_frame = -1;
  bool seen_below = false;
  double max_after_below = 0;
  std::vector<double> errors;
  std::vector<double> ages;

  for (int frame = 0; frame < config_.frames; ++frame) {
    const std::int64_t ts_ns =
        static_cast<std::int64_t>(std::llround(frame * config_.dt_s * 1e9));

    const bool connected=!(config_.disconnect_start>=0 && frame>=config_.disconnect_start &&
                           frame<config_.disconnect_start+config_.disconnect_frames);
    bool dispatch_ok=true;
    auto applyDueCommands=[&](SimTime now, bool link_up) {
      std::stable_sort(transit.begin(),transit.end(),[](const auto& a,const auto& b){return a.arrival<b.arrival;});
      while((!transit.empty() && transit.front().arrival<=now) ||
            (stop_request_ready>=0 && stop_request_ready<=now)) {
        // Stop wins ties and cancels older in-flight commands at the device barrier.
        if(stop_request_ready>=0 && stop_request_ready<=now &&
           (transit.empty() || stop_request_ready<=transit.front().arrival)) {
          const auto time=stop_request_ready;stop_request_ready=-1;
          if(link_up) {
            stopAt(time);
            if(stop_request_nonce) replyStop(time,stop_request_epoch,stop_request_nonce,true);
          }
          continue;
        }
        auto c=transit.front();transit.erase(transit.begin());
        apply(c.arrival);
        if(actuator.submit(c)) {
          if(active_command.id) event("replaced",active_command,c.arrival,0,0);
          active_command=c;event("accepted",c,c.arrival,0,0);
        } else event("rejected",c,c.arrival,0,0);
      }
      apply(now);
    };
    auto deliverFeedbackNow=[&](SimTime now, int event_frame, bool link_up) {
      if(config_.feedback_transport) {
        feedback_channel.deliver(now);
        for(const auto& trace:feedback_channel.traces()) {
          if(trace.received==now && trace.status=="applied")
            flight.deliver(trace.snapshot.command_id,trace.snapshot.applied_x,trace.snapshot.applied_y,trace.snapshot.terminal);
        }
      }
      const bool feedback_overdue=config_.feedback_timeout_ms>0 && (config_.feedback_transport ?
        feedback_channel.overdue(now,std::llround(config_.feedback_timeout_ms*1e6)) :
        feedback.overdue(now,std::llround(config_.feedback_timeout_ms*1e6)));
      if(feedback_overdue || (config_.feedback_transport && feedback_channel.full())) {
        feedback_capacity_fault=config_.feedback_transport && feedback_channel.full();
        if(!feedback_fault && config_.stop_request_delay_ms>0) {
          stop_request_ready=now+std::llround(config_.stop_request_delay_ms*1e6);
          stop_request_nonce=0; // automatic stop cannot authorize recovery
          if(config_.async_stages && !stages.enqueue(Stage::Execution,stop_request_ready,event_frame,0))
            ++output.stage_rejected;
        }
        feedback_fault=true;feedback.clear();flight.clear();
        if(config_.feedback_transport) feedback_channel.newEpoch(now);
      }
      // Explicit recovery request; a received idle snapshot confirms the model
      // has cancelled active work and drained transport. Time alone never clears it.
      if(feedback_fault && !feedback_resynced && config_.feedback_resync_frame>=0 &&
         event_frame>=config_.feedback_resync_frame && link_up) {
        const bool idle_snapshot=actuator.idle() && transit.empty() && active_command.id==0;
        bool confirmed=false;
        if(config_.feedback_transport) {
          // Only one response is retained; expiry permits a fresh nonce.
          // Check an arriving response before issuing a replacement request.
          if(stop_ready>=0 && stop_ready<=now) {
            confirmed=stop_confirmation.accept(stop_snapshot,now,feedback_channel.epoch(),idle_snapshot);
            stop_ready=-1;
          }
          if(!confirmed && !stop_confirmation.pending(now) && stop_request_ready<0) {
            ++feedback_sync_attempts;
            const auto nonce=stop_confirmation.request(feedback_channel.epoch(),now,
                std::llround(config_.feedback_timeout_ms*1e6));
            stop_ready=-1;
            if(config_.stop_request_delay_ms>0) {
              stop_request_ready=now+std::llround(config_.stop_request_delay_ms*1e6);
              stop_request_nonce=nonce;stop_request_epoch=feedback_channel.epoch();
              if(config_.async_stages && !stages.enqueue(Stage::Execution,stop_request_ready,event_frame,0))
                ++output.stage_rejected;
            } else {
              replyStop(now,feedback_channel.epoch(),nonce,idle_snapshot);
              if(stop_ready==now) {
                confirmed=stop_confirmation.accept(stop_snapshot,now,feedback_channel.epoch(),idle_snapshot);
                stop_ready=-1;
              }
            }
          }
        } else {
          ++feedback_sync_attempts;
          if(dropFeedback(now)) ++feedback_dropped;
          else confirmed=idle_snapshot;
        }
        if(confirmed) {
          feedback.clear();flight.clear();pid_x.reset();pid_y.reset();
          if(config_.feedback_transport) feedback_channel.newEpoch(now);
          feedback_fault=false;feedback_resynced=true;
          feedback_capacity_fault=false;
        }
      }
    };
    auto publishOne=[&](int frame_id, SimTime released) {
      auto it=std::find_if(observations.begin(),observations.end(),[&](const auto& item){return item.frame==frame_id;});
      if(it==observations.end()) return;
      const auto observation=*it;
      observations.erase(it);
      auto& trace=output.observations[observation.frame];
      trace.released=released;
      if(config_.encoder_transport && !encoder_channel.at(observation.captured)) {
        trace.status="encoder_missing";
        return;
      }
      if(observation.captured<=tracker.snapshot().timestamp) {
        trace.status="stale";
        return;
      }
      mot::CameraMotionEstimate motion;
      const auto paired_encoder=config_.encoder_transport ? encoder_channel.at(observation.captured) :
          std::optional<EncoderSample>{{observation.captured,observation.encoder_x,observation.encoder_y}};
      motion.from=tracker.snapshot().timestamp;motion.to=observation.captured;
      motion.accepted=config_.encoder_compensation && motion.from>=0;
      motion.shift={-(paired_encoder->x-observed_encoder_x),-(paired_encoder->y-observed_encoder_y)};
      motion.variance=.25;motion.reason="simulated_applied_encoder";
      if(!tracker.update(observation.frame,observation.captured,observation.detections,
                         config_.encoder_compensation ? &motion : nullptr))
        throw std::runtime_error("Invalid published observation");
      trace.status="published";observation_frame=observation.frame;
      observed_encoder_x=paired_encoder->x;observed_encoder_y=paired_encoder->y;
    };
    auto handleStage=[&](const StageEvent& event_stage) {
      output.stages.push_back({name(event_stage.stage), event_stage.time_ns, event_stage.frame});
      const bool link_up=!(config_.disconnect_start>=0 && event_stage.frame>=config_.disconnect_start &&
                           event_stage.frame<config_.disconnect_start+config_.disconnect_frames);
      if(event_stage.stage==Stage::Inference) publishOne(event_stage.frame, event_stage.time_ns);
      if(event_stage.stage==Stage::Feedback && config_.incremental_control)
        deliverFeedbackNow(event_stage.time_ns, event_stage.frame, link_up);
      if(event_stage.stage==Stage::Execution && config_.incremental_control)
        applyDueCommands(event_stage.time_ns, link_up);
    };
    if(config_.incremental_control && !config_.async_stages) {
      applyDueCommands(ts_ns, connected);
      deliverFeedbackNow(ts_ns, frame, connected);
    } else if(config_.async_stages) {
      // Older events must not observe an encoder clock already moved to this frame.
      while(auto due=stages.popDue(ts_ns-1)) {
        if(config_.encoder_transport) encoder_channel.advance(due->time_ns);
        handleStage(*due);
      }
    }

    if (config_.motion == Motion::Stop && frame >= config_.frames / 3) {
      vx = 0;
      vy = 0;
    }
    if (config_.motion == Motion::Turn && frame == config_.frames / 2) {
      vx = -vx;
      vy = -vy;
      if (vx == 0 && vy == 0) vx = -120;
    }
    if (config_.motion == Motion::Squat && frame >= config_.frames / 3 &&
        height > 24) {
      height = std::max(24.0f, height - 2.0f);
      world_y = bottom - height * 0.5f;
    }
    if (config_.motion == Motion::CameraPan && frame >= config_.frames / 20) {
      camera_x += config_.camera_vx * dt;
    }

    world_x += vx * dt;
    world_y += vy * dt;
    if(config_.distractor) {
      distractor_x += config_.distractor_vx * dt;
      distractor_y += config_.distractor_vy * dt;
    }

    if (config_.motion != Motion::CameraPan) {
      const float min_x = config_.target_w * 0.5f;
      const float max_x = static_cast<float>(config_.width) - config_.target_w * 0.5f;
      const float min_y = config_.target_h * 0.5f;
      const float max_y = static_cast<float>(config_.height) - config_.target_h * 0.5f;
      if (world_x < min_x) { world_x = min_x; vx = -vx; }
      if (world_x > max_x) { world_x = max_x; vx = -vx; }
      if (world_y < min_y) { world_y = min_y; vy = -vy; }
      if (world_y > max_y) { world_y = max_y; vy = -vy; }
    }

    double human_dx=0, human_dy=0;
    bool human_explicit_frame=false, human_pulse_frame=false, human_release_frame=false;
    if(config_.assist_mode==AssistMode::HumanLed) {
      while(human_cursor<config_.human_camera.size() && config_.human_camera[human_cursor].time_ns<=ts_ns) {
        const auto& sample=config_.human_camera[human_cursor++];
        human_dx+=sample.dx_px;
        human_dy+=sample.dy_px;
        const bool large_turn=std::hypot(sample.dx_px,sample.dy_px)>=config_.human_turn_px;
        if(sample.takeover) {
          human_hold=true;
          human_explicit_frame=true;
        } else if(human_hold) {
          human_hold=false;
          human_release_frame=true;
        }
        if(!sample.takeover && large_turn) human_pulse_frame=true;
        if(config_.view_feedback) {
          camera_x+=static_cast<float>(sample.dx_px);
          camera_y+=static_cast<float>(sample.dy_px);
          encoder_x+=sample.dx_px;
          encoder_y+=sample.dy_px;
        } else {
          aim_x+=static_cast<float>(sample.dx_px);
          aim_y+=static_cast<float>(sample.dy_px);
        }
      }
    }
    const float truth_x = config_.view_feedback ? world_x-camera_x : clampf(world_x - camera_x,
                                 config_.target_w * 0.5f,
                                 static_cast<float>(config_.width) - config_.target_w * 0.5f);
    const float truth_y = config_.view_feedback ? world_y-camera_y : clampf(world_y,
                                 config_.target_h * 0.5f,
                                 static_cast<float>(config_.height) - config_.target_h * 0.5f);
    // Later same-timestamp execution must not rewrite this sample's camera.
    const float sample_camera_x=camera_x, sample_camera_y=camera_y;
    const bool occluded = config_.occlusion_start >= 0 &&
                          frame >= config_.occlusion_start && frame < occlusion_end;

    std::vector<mot::Detection> detections;
    bool detected = false;
    bool false_positive_this_frame = false;
    const bool in_view=truth_x+config_.target_w*.5f>0 && truth_x-config_.target_w*.5f<config_.width &&
                       truth_y+height*.5f>0 && truth_y-height*.5f<config_.height;
    if(images_.detect) {
      cv::Mat background,raster;
      cv::resize(images_.background,background,{config_.width,config_.height});
      const cv::Mat affine=(cv::Mat_<double>(2,3)<<1,0,-camera_x,0,1,-camera_y);
      cv::warpAffine(background,raster,affine,background.size(),cv::INTER_LINEAR,cv::BORDER_WRAP);
      if(!occluded && in_view) {
        const auto anchor=images_.sprite_target;
        const double sx=config_.target_w/anchor.width,sy=height/anchor.height;
        if(config_.subpixel_render) {
          const double tx=truth_x-(anchor.x+anchor.width*.5)*sx;
          const double ty=truth_y-(anchor.y+anchor.height*.5)*sy;
          const cv::Mat transform=(cv::Mat_<double>(2,3)<<sx,0,tx,0,sy,ty);
          cv::Mat foreground,alpha;
          cv::warpAffine(images_.sprite,foreground,transform,raster.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT);
          cv::Mat mask(images_.sprite.size(),CV_32F,cv::Scalar(1));
          cv::warpAffine(mask,alpha,transform,raster.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT);
          // foreground already has zero-border interpolation (premultiplied coverage).
          for(int y=0;y<raster.rows;++y) for(int x=0;x<raster.cols;++x) {
            const float a=alpha.at<float>(y,x);
            auto& dst=raster.at<cv::Vec3b>(y,x);const auto src=foreground.at<cv::Vec3b>(y,x);
            for(int c=0;c<3;++c) dst[c]=cv::saturate_cast<unsigned char>(src[c]+dst[c]*(1-a));
          }
        } else {
        cv::Rect destination(cvRound(truth_x-(anchor.x+anchor.width*.5)*sx),
                             cvRound(truth_y-(anchor.y+anchor.height*.5)*sy),
                             std::max(1,cvRound(images_.sprite.cols*sx)),std::max(1,cvRound(images_.sprite.rows*sy)));
        cv::Mat sprite;cv::resize(images_.sprite,sprite,destination.size());
        const cv::Rect visible=destination & cv::Rect(0,0,raster.cols,raster.rows);
        if(visible.area()>0) sprite(cv::Rect(visible.x-destination.x,visible.y-destination.y,visible.width,visible.height)).copyTo(raster(visible));
        }
      }
      detections=images_.detect(raster,frame,ts_ns);
      detected=!detections.empty();
      if(!detected && !occluded) ++missed;
    } else if (!occluded && in_view && unit(rng) >= config_.dropout) {
      detected = true;
      const float sx = truth_x + noise(rng);
      const float sy = truth_y + noise(rng);
      const float score = unit(rng) < config_.low_confidence_rate
                              ? config_.low_confidence_score
                              : 0.9f;
      const cv::Rect2f person{sx - config_.target_w * 0.5f,
                              sy - height * 0.5f,
                              config_.target_w, height};
      detections.push_back({1, score, person});

      const float head_h = std::max(6.0f, height * 0.22f);
      const float head_w = std::max(4.0f, config_.target_w * 0.45f);
      const float head_cy = sy - height * 0.5f + head_h * 0.55f;
      const cv::Rect2f head{sx - head_w * 0.5f, head_cy - head_h * 0.5f,
                            head_w, head_h};
      detections.push_back({0, score, head});
    } else if (!occluded) {
      ++missed;
    }

    if (!images_.detect && unit(rng) < config_.false_positive_rate) {
      const float fp_x = 20.0f + unit(rng) * (config_.width - 40.0f);
      const float fp_y = 20.0f + unit(rng) * (config_.height - 40.0f);
      detections.push_back({1, 0.5f,
                            {fp_x - 10.0f, fp_y - 20.0f, 20.0f, 40.0f}});
      ++total_false_positives;
      false_positive_this_frame = true;
    }
    if(!images_.detect && config_.distractor) {
      const float sx = config_.view_feedback ? distractor_x-camera_x : distractor_x;
      const float sy = config_.view_feedback ? distractor_y-camera_y : distractor_y;
      const float box_w=config_.distractor_w, box_h=config_.distractor_h;
      const bool other_in_view=sx+box_w*.5f>0 && sx-box_w*.5f<config_.width &&
                               sy+box_h*.5f>0 && sy-box_h*.5f<config_.height;
      if(other_in_view) {
        detections.push_back({1, 0.85f, {sx-box_w*.5f, sy-box_h*.5f, box_w, box_h}});
        const float head_h=std::max(6.0f, box_h*0.22f);
        const float head_w=std::max(4.0f, box_w*0.45f);
        const float head_cy=sy-box_h*.5f+head_h*0.55f;
        detections.push_back({0, 0.85f, {sx-head_w*.5f, head_cy-head_h*.5f, head_w, head_h}});
        detected=true;
      }
    }

    total_detections += static_cast<int>(detections.size());
    const double service_ms=images_.detect && images_.service_time_ms ? images_.service_time_ms() : config_.inference_service_ms;
    if(!std::isfinite(service_ms) || service_ms<0 || service_ms>10000) throw std::runtime_error("Invalid service duration");
    service_finished=std::max(ts_ns,service_finished)+std::llround(service_ms*1e6);
    auto ready=service_finished+std::llround(std::max(0.0,config_.observation_delay_ms+observation_jitter(observation_rng))*1e6);
    if(config_.encoder_transport) {
      const bool dropped=(config_.encoder_drop_end_frame<0 || frame<config_.encoder_drop_end_frame) &&
          config_.encoder_drop_rate>0 && failure(encoder_rng)<config_.encoder_drop_rate;
      encoder_channel.publish({ts_ns,encoder_x,encoder_y},dropped);
      encoder_channel.advance(ts_ns);
      ready=std::max(ready,ts_ns+std::llround(config_.encoder_delay_ms*1e6));
    }
    output.observations.push_back({frame,ts_ns,ready,-1,service_ms,"pending"});
    observations.push_back({frame,ts_ns,ready,detections,encoder_x,encoder_y});
    std::stable_sort(observations.begin(),observations.end(),[](const auto& a,const auto& b){return a.ready<b.ready;});
    if(config_.async_stages) {
      if(!stages.enqueue(Stage::Capture, ts_ns, frame)) ++output.stage_rejected;
      if(!stages.enqueue(Stage::Inference, ready, frame)) {++output.stage_rejected; stages.release(frame);}
      if(!stages.enqueue(Stage::Feedback, ts_ns, frame)) ++output.stage_rejected;
      while(auto due=stages.popDue(ts_ns)) {
        if(config_.encoder_transport) encoder_channel.advance(due->time_ns);
        handleStage(*due);
      }
    } else {
      while(!observations.empty() && observations.front().ready<=ts_ns)
        publishOne(observations.front().frame, ts_ns);
    }
    while(observations.size()>size_t(config_.observation_capacity)) {
      auto oldest=std::min_element(observations.begin(),observations.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
      auto& trace=output.observations[oldest->frame];trace.status="overflow";trace.released=ts_ns;
      observations.erase(oldest);
    }
    const auto received_encoder=encoder_channel.latest();
    const auto extrapolated=(config_.encoder_extrapolation || delay_mode) ?
        encoder_channel.extrapolatedPosition(config_.actuator_max_speed) : std::optional<std::pair<double,double>>{};
    const bool encoder_valid=!config_.encoder_transport ||
        (received_encoder && observation_frame>=0 && received_encoder->captured>=tracker.snapshot().timestamp &&
         (!config_.encoder_extrapolation || extrapolated.has_value()));
    const double delivered_x=config_.encoder_transport ? (received_encoder?received_encoder->x:observed_encoder_x) : encoder_x;
    const double delivered_y=config_.encoder_transport ? (received_encoder?received_encoder->y:observed_encoder_y) : encoder_y;
    const double available_x=(config_.encoder_extrapolation && extrapolated) ? extrapolated->first : delivered_x;
    const double available_y=(config_.encoder_extrapolation && extrapolated) ? extrapolated->second : delivered_y;
    const double recent_dx=config_.encoder_compensation && encoder_valid ? available_x-observed_encoder_x : 0;
    const double recent_dy=config_.encoder_compensation && encoder_valid ? available_y-observed_encoder_y : 0;
    auto snapshot=tracker.predictAt(ts_ns);
    for(auto& track:snapshot.tracks) {track.x[0]-=recent_dx;track.x[1]-=recent_dy;}
    const mot::Track* selected = nullptr;
    if (!selection.selected) {
      if (config_.acquire_single_confirmed) {
        const mot::Track* candidate = nullptr;
        int eligible = 0;
        for (const auto& track : snapshot.tracks) {
          if (track.state == mot::State::Confirmed && !track.ambiguous) {
            candidate = &track;
            ++eligible;
          }
        }
        if (eligible == 1) selection.select(candidate->id, snapshot);
      } else {
        selection.select(static_cast<mot::Id>(config_.selected_track_id), snapshot);
      }
    }
    for (const auto& track : snapshot.tracks) {
      if (selection.selected == track.id) {
        selected = &track;
        break;
      }
    }
    const double track_speed=selected ? std::hypot(selected->x[2],selected->x[3]) : 0;
    const bool legacy_chase=config_.assist_mode==AssistMode::Legacy;
    const bool auto_follow=legacy_chase && delay_mode && !config_.velocity_feedforward && track_speed>5;
    if(auto_follow && !config_.encoder_extrapolation) {
      const double lead=config_.encoder_delay_ms*1e-3;
      for(auto& track:snapshot.tracks) {
        track.x[0]-=track.x[2]*lead;
        track.x[1]-=track.x[3]*lead;
      }
    }

    if (selected && last_track_id != 0 && selected->id != last_track_id) {
      ++id_switches;
    }
    if (selected) last_track_id = selected->id;
    if (selected && selected->state == mot::State::Confirmed) ++confirmed_frames;

    const bool has_track = selected != nullptr;
    const std::uint64_t track_id = selected ? selected->id : 0;
    const bool ambiguous = selected ? selected->ambiguous : true;
    const float age_ms =
        selected ? static_cast<float>((ts_ns - selected->lastSeen) * 1e-6) : 0;
    double covariance_trace = 0;
    if(selected) {
      // x' = x - lead*vx when the command uses that shifted estimate. The gate
      // sees that estimate's variance at the control instant, not the earlier
      // measurement covariance.
      const double lead=(auto_follow && !config_.encoder_extrapolation) ? config_.encoder_delay_ms*1e-3 : 0;
      auto axis=[&](int position, int velocity) {
        const double var=selected->covariance(position,position)
            -2*lead*selected->covariance(position,velocity)
            +lead*lead*selected->covariance(velocity,velocity);
        return var>0 ? var : 0;
      };
      covariance_trace=axis(0,2)+axis(1,3);
    }

    float estimate_x = last_est_x;
    float estimate_y = last_est_y;
    float estimate_vx = 0;
    float estimate_vy = 0;
    float predicted_x = last_pred_x;
    float predicted_y = last_pred_y;
    if (selected) {
      estimate_x = static_cast<float>(selected->x[0]);
      estimate_y = static_cast<float>(selected->x[1]);
      estimate_vx = static_cast<float>(selected->x[2]);
      estimate_vy = static_cast<float>(selected->x[3]);
      predicted_x = estimate_x + static_cast<float>(selected->x[2]) *
                                     config_.command_horizon_s;
      predicted_y = estimate_y + static_cast<float>(selected->x[3]) *
                                     config_.command_horizon_s;
      last_est_x = estimate_x;
      last_est_y = estimate_y;
      last_pred_x = predicted_x;
      last_pred_y = predicted_y;
    }
    if (selected && selected->imm) {
      auto forecast=tracker.predictAt(ts_ns+std::llround(config_.command_horizon_s*1e9));
      for(const auto& future:forecast.tracks) if(future.id==selected->id) {
        predicted_x=static_cast<float>(future.x[0]-recent_dx);
        predicted_y=static_cast<float>(future.x[1]-recent_dy);
      }
      last_pred_x=predicted_x; last_pred_y=predicted_y;
    }
    predicted_x = clampf(predicted_x, 0, static_cast<float>(config_.width));
    predicted_y = clampf(predicted_y, 0, static_cast<float>(config_.height));

    // Only actionable observations may produce a new control command.
    const bool controllable = selected && selected->state == mot::State::Confirmed &&
                              !selected->ambiguous &&
                              age_ms <= config_.max_age_ms &&
                              covariance_trace <= config_.covariance_limit;
    std::pair<float,float> target{aim_x,aim_y};
    double command_lead_ms=-1;
    double used_horizon=0;
    bool assist_cancel_frame=false;
    double assist_dx_frame=0, assist_dy_frame=0;
    std::string assist_reason=config_.assist_mode==AssistMode::HumanLed ? "outside" : "legacy";
    if (controllable) {
      const float horizon = auto_follow ? 0.f : (config_.feedback_prediction || (delay_mode && !config_.velocity_feedforward)) ? float(feedbackDelay()) : config_.compensate_actuator_delay
                                ? config_.actuator_delay_frames * dt
                                : config_.command_horizon_s;
      used_horizon=horizon;
      target.first = clampf(estimate_x + static_cast<float>(selected->x[2])*horizon, 0,
                            static_cast<float>(config_.width));
      target.second = clampf(estimate_y + static_cast<float>(selected->x[3])*horizon, 0,
                             static_cast<float>(config_.height));
      if(selected->imm) {
        auto future=tracker.predictAt(ts_ns+std::llround(horizon*1e9));
        for(const auto& track:future.tracks) if(track.id==selected->id) {
          target.first=clampf(static_cast<float>(track.x[0]-recent_dx),0,static_cast<float>(config_.width));
          target.second=clampf(static_cast<float>(track.x[1]-recent_dy),0,static_cast<float>(config_.height));
        }
      }
    }
    auto dispatch_incremental=[&](const IncrementCommand& command, double vx_cmd, double vy_cmd) {
      transit.push_back(command);
      event("generated",transit.back(),command.generated,command.dx,command.dy);
      if(config_.dispatch_failure_rate>0 && failure(failure_rng)<config_.dispatch_failure_rate) {
        event("dispatch_failed",transit.back(),command.generated,0,0);
        transit.pop_back();
        dispatch_ok=false;
        pid_x.reset(); pid_y.reset();
        return false;
      }
      if(config_.async_stages && !stages.enqueue(Stage::Execution, command.arrival, static_cast<int>(command.frame_id), 1))
        ++output.stage_rejected;
      if(std::hypot(vx_cmd,vy_cmd)>=config_.actuator_max_speed*config_.actuator_units_per_px) ++saturated_frames;
      return true;
    };
    // Accepted assist commands are tracked by the deadline chosen at issue time.
    // The assist branch does not read the actuator or active_command. A transport
    // or delayed stop is queued; otherwise an already accepted command runs until
    // that known deadline and is the residual motion.
    auto withdraw_assist=[&]{
      assist_speed_x=assist_speed_y=0;
      pid_x.reset(); pid_y.reset();
      for(auto it=transit.begin(); it!=transit.end();) {
        if(it->arrival>ts_ns) {
          event("cancelled_in_transit",*it,ts_ns,0,0);
          if(it->expires==assist_known_until) assist_known_until=-1;
          it=transit.erase(it);
          assist_cancel_frame=true;
        } else ++it;
      }
      const bool remote_stop=config_.feedback_transport || config_.stop_request_delay_ms>0;
      if(remote_stop && assist_known_until>ts_ns && stop_request_ready<0) {
        stop_request_ready=ts_ns+std::llround(std::max(0.0,config_.stop_request_delay_ms)*1e6);
        assist_cancel_frame=true;
        // Async execution only runs when an execution event is popped. The stop
        // has to be that event; a later command must not be required to notice it.
        // Rank 0 is popped before a command scheduled at the same time.
        if(config_.async_stages && !stages.enqueue(Stage::Execution, stop_request_ready, frame, 0))
          ++output.stage_rejected;
      }
    };
    if(legacy_chase && config_.incremental_control) {
      if(controllable && connected && !feedback_fault && encoder_valid) {
        const double delay=std::max(0.0,config_.actuator_delay_frames*config_.dt_s+jitter(delay_rng)*.001);
        const auto arrival=ts_ns+std::llround(delay*1e9);
        command_lead_ms=(arrival-ts_ns)*1e-6;
        const bool account_pending=config_.feedback_prediction || (delay_mode && !auto_follow && !config_.velocity_feedforward);
        const bool feed=config_.velocity_feedforward || auto_follow;
        const auto pending=(feed && delay_mode) ? flight.pending() : account_pending ? (config_.feedback_transport?feedback_channel.pending():feedback.pending()) : std::pair<double,double>{};
        const double fx=feed ? selected->x[2]*config_.actuator_units_per_px : 0;
        const double fy=feed ? selected->x[3]*config_.actuator_units_per_px : 0;
        const double ux=pid_x.step((target.first-aim_x)*config_.actuator_units_per_px-pending.first,config_.dt_s,fx);
        const double uy=pid_y.step((target.second-aim_y)*config_.actuator_units_per_px-pending.second,config_.dt_s,fy);
        IncrementCommand command{std::uint64_t(frame+1),std::uint64_t(frame),selected->id,
                          ts_ns,arrival,arrival+std::llround(2*config_.dt_s*1e9),ux*config_.dt_s,uy*config_.dt_s};
        if(feed && delay_mode)
          flight.issue(command.id,(ux-fx)*config_.dt_s,(uy-fy)*config_.dt_s,ux*config_.dt_s,uy*config_.dt_s);
        dispatch_incremental(command,ux,uy);
      } else if(feedback_fault && connected && config_.stop_request_delay_ms>0) {
        // Block generation immediately; physical stop waits for transport.
        pid_x.reset();pid_y.reset();
      } else {
        for(const auto& c:transit) event(connected ? "cancelled_in_transit" : "disconnected_in_transit",c,ts_ns,0,0);
        if(active_command.id) event(connected ? "cancelled" : "disconnected",active_command,ts_ns,0,0);
        active_command={};transit.clear();actuator.cancel();pid_x.reset();pid_y.reset();
      }
    } else if(config_.assist_mode==AssistMode::HumanLed && config_.incremental_control && !config_.assist_enabled) {
      assist_state=AssistState::Disabled;
      assist_reason="assist_off";
      assist_latched=false;
      withdraw_assist();
    } else if(config_.assist_mode==AssistMode::HumanLed && config_.incremental_control) {
      const double center_x=output.assist.center_x, center_y=output.assist.center_y;
      const double image_dx=selected ? selected->x[0]-center_x : 0;
      const double image_dy=selected ? selected->x[1]-center_y : 0;
      const double distance=std::hypot(image_dx,image_dy);
      if(human_hold) {
        assist_state=AssistState::HumanOverride;
        assist_reason=human_explicit_frame ? "human_turn" : "human_hold";
        assist_latched=false;
        withdraw_assist();
      } else if(human_release_frame || human_pulse_frame) {
        assist_state=AssistState::HumanOverride;
        assist_reason=human_pulse_frame && !human_release_frame ? "human_turn" : "resume_wait";
        assist_latched=false;
        assist_resume_at=ts_ns+std::llround(config_.assist_resume_wait_s*1e9);
        withdraw_assist();
      } else if(assist_state==AssistState::HumanOverride && ts_ns<assist_resume_at) {
        assist_reason="resume_wait";
        withdraw_assist();
      } else if(!controllable || feedback_fault || !connected || !encoder_valid) {
        assist_state=AssistState::Suspended;
        assist_reason=feedback_fault ? "feedback_fault" : !encoder_valid ? "encoder_unavailable" :
            !connected ? "disconnected" : "unreliable";
        assist_latched=false;
        withdraw_assist();
      } else {
        if(!assist_latched && distance<=config_.assist_enter_px) assist_latched=true;
        else if(assist_latched && distance>config_.assist_exit_px) assist_latched=false;
        if(!assist_latched) {
          assist_state=AssistState::Armed;
          assist_reason="outside";
          withdraw_assist();
        } else if(assist_known_until>ts_ns) {
          // The deadline was chosen when the command was issued. Hidden device
          // progress does not change this decision.
          assist_state=AssistState::Assisting;
          assist_reason="in_flight";
        } else {
          assist_state=AssistState::Assisting;
          const double dt=config_.dt_s;
          const double room=std::max(0.0, distance-config_.assist_deadzone_px);
          const double step_cap=std::min(config_.assist_max_step_px, config_.assist_max_speed_px_s*dt);
          const double correction=distance>1e-9 ? std::min(room, step_cap) : 0;
          const double ux=distance>1e-9 ? image_dx/distance : 0;
          const double uy=distance>1e-9 ? image_dy/distance : 0;
          // Add the track's image velocity, then clamp to the same experimental
          // step. A slow walk inside the region stays near center; the step
          // still cannot grow into a chase.
          double want_x=ux*correction+selected->x[2]*dt;
          double want_y=uy*correction+selected->x[3]*dt;
          const double want=std::hypot(want_x,want_y);
          if(want>step_cap && want>0) {want_x*=step_cap/want; want_y*=step_cap/want;}
          if(room<=0 && want<0.05) {
            assist_reason="deadzone";
            assist_speed_x=assist_speed_y=0;
          } else {
            assist_reason=room<=0 ? "hold_velocity" : "assisting";
            double vx_cmd=want_x/dt, vy_cmd=want_y/dt;
            double dvx=vx_cmd-assist_speed_x, dvy=vy_cmd-assist_speed_y;
            const double dv=std::hypot(dvx,dvy);
            const double max_dv=config_.assist_max_accel_px_s2*dt;
            if(dv>max_dv && dv>0) {dvx*=max_dv/dv; dvy*=max_dv/dv;}
            assist_speed_x+=dvx; assist_speed_y+=dvy;
            const double speed=std::hypot(assist_speed_x,assist_speed_y);
            if(speed>config_.assist_max_speed_px_s) {
              assist_speed_x*=config_.assist_max_speed_px_s/speed;
              assist_speed_y*=config_.assist_max_speed_px_s/speed;
            }
            assist_dx_frame=assist_speed_x*dt;
            assist_dy_frame=assist_speed_y*dt;
            const double step=std::hypot(assist_dx_frame,assist_dy_frame);
            if(step>config_.assist_max_step_px) {
              assist_dx_frame*=config_.assist_max_step_px/step;
              assist_dy_frame*=config_.assist_max_step_px/step;
            }
            if(std::hypot(assist_dx_frame,assist_dy_frame)>1e-6 && connected && !feedback_fault && encoder_valid) {
              const double delay=std::max(0.0,config_.actuator_delay_frames*config_.dt_s+jitter(delay_rng)*.001);
              const auto arrival=ts_ns+std::llround(delay*1e9);
              command_lead_ms=(arrival-ts_ns)*1e-6;
              used_horizon=0;
              // Keep the existing two-step lifetime so an in-flight trim can
              // still be stopped. The device itself is not read.
              IncrementCommand command{std::uint64_t(frame+1),std::uint64_t(frame),selected->id,
                                ts_ns,arrival,arrival+std::llround(2*config_.dt_s*1e9),
                                assist_dx_frame*config_.actuator_units_per_px,
                                assist_dy_frame*config_.actuator_units_per_px};
              if(dispatch_incremental(command,command.dx/config_.dt_s,command.dy/config_.dt_s))
                assist_known_until=command.expires;
            }
          }
        }
      }
    } else if(legacy_chase) {
    command_queue.push_back(target);
    std::pair<float,float> command{aim_x,aim_y};
    if (command_queue.size() > static_cast<size_t>(config_.actuator_delay_frames)) {
      command = command_queue.front();
      command_queue.pop_front();
    }
    const float max_step = config_.actuator_max_speed * dt;
    const float dx = command.first - aim_x;
    const float dy = command.second - aim_y;
    const float distance = std::hypot(dx, dy);
    if (distance > max_step) {
      aim_x += dx / distance * max_step;
      aim_y += dy / distance * max_step;
      ++saturated_frames;
    } else {
      aim_x = command.first;
      aim_y = command.second;
    }

    }

    const float error_x = aim_x - truth_x;
    const float error_y = aim_y - truth_y;
    const float error_px = std::hypot(error_x, error_y);
    const float error_norm = normalizedError(error_x, error_y, config_.target_w, height);

    const bool valid = controllable && connected && dispatch_ok && !feedback_fault && encoder_valid;

    FrameRecord record;
    record.control_valid=valid;
    record.encoder_valid=encoder_valid;
    record.encoder_sample_ts_ns=received_encoder?received_encoder->captured:-1;
    record.encoder_dropped=encoder_channel.dropped();record.encoder_overflow=encoder_channel.overflow();
    record.encoder_queued=encoder_channel.queued();
    record.observation_ts_ns=tracker.snapshot().timestamp;record.observation_frame=observation_frame;
    record.observation_age_ms=record.observation_ts_ns>=0 ? (ts_ns-record.observation_ts_ns)*1e-6 : -1;
    record.encoder_age_ms=record.encoder_sample_ts_ns>=0 ? (ts_ns-record.encoder_sample_ts_ns)*1e-6 : -1;
    record.command_lead_ms=command_lead_ms;
    record.estimated_delay_ms=feedbackDelay()*1000;
    record.feedback_dropped=feedback_dropped;record.feedback_fault=feedback_fault;record.feedback_pending=feedbackSize();
    record.feedback_delivered=feedback_channel.delivered();record.feedback_ignored=feedback_channel.ignored();
    record.feedback_invalid=feedback_channel.invalid();record.feedback_overflow=feedback_channel.overflow();
    record.feedback_queued=feedback_channel.queued();record.feedback_epoch=config_.feedback_transport?feedback_channel.epoch():0;
    record.feedback_resynced=feedback_resynced;record.feedback_sync_attempts=feedback_sync_attempts;
    record.frame = frame;
    record.ts_ns = ts_ns;
    record.camera_x=sample_camera_x;record.camera_y=sample_camera_y;
    record.truth_x = truth_x;
    record.truth_y = truth_y;
    record.occluded = occluded;
    record.observed = detected;
    record.has_track = has_track;
    record.track_id = track_id;
    if(selected && selected->imm) {
      record.has_modes=true;
      record.mode_probabilities=selected->imm->probabilities;
    }
    record.track_state = selected ? mot::name(selected->state) : "None";
    record.ambiguous = ambiguous;
    record.control_gate=feedback_fault ? (feedback_capacity_fault?"feedback_capacity":"feedback_timeout") : !encoder_valid ? "encoder_unavailable" : !connected ? "actuator_disconnected" : !dispatch_ok ? "dispatch_failed" : !selected ? "no_selected_track" : selected->state!=mot::State::Confirmed ? "not_confirmed" :
      selected->ambiguous ? "ambiguous" :
      age_ms>config_.max_age_ms ? "stale" : covariance_trace>config_.covariance_limit ? "uncertain" : "ready";
    record.est_x = estimate_x;
    record.est_y = estimate_y;
    record.est_vx = estimate_vx;
    record.est_vy = estimate_vy;
    record.pred_x = predicted_x;
    record.pred_y = predicted_y;
    record.aim_x = aim_x;
    record.aim_y = aim_y;
    record.error_px = error_px;
    record.error_norm = error_norm;
    record.age_ms = age_ms;
    record.covariance_trace = covariance_trace;
    record.detections = static_cast<int>(detections.size());
    record.false_positives = false_positive_this_frame ? 1 : 0;
    record.assist_state=name(assist_state);
    record.assist_reason=assist_reason;
    record.assist_cancel=assist_cancel_frame;
    record.assist_dx_px=assist_dx_frame;
    record.assist_dy_px=assist_dy_frame;
    record.human_dx_px=human_dx;
    record.human_dy_px=human_dy;
    record.human_takeover=human_explicit_frame || human_pulse_frame;
    record.human_sample=human_explicit_frame || human_pulse_frame || human_release_frame || human_dx!=0 || human_dy!=0;
    record.human_explicit=human_explicit_frame;
    record.human_release=human_release_frame;
    record.executed_dx_px=config_.view_feedback ? sample_camera_x-previous_camera_x : aim_x-previous_aim_x;
    record.executed_dy_px=config_.view_feedback ? sample_camera_y-previous_camera_y : aim_y-previous_aim_y;
    record.auto_follow=auto_follow;
    record.feed_active=config_.velocity_feedforward || auto_follow;
    record.used_horizon_s=used_horizon;
    record.gate_transform=(auto_follow && !config_.encoder_extrapolation && config_.encoder_delay_ms>0)
        ? "predicted_with_lead" : "predicted";
    if(record.auto_follow) ++output.effective.auto_follow_frames;
    if(record.feed_active) ++output.effective.feed_frames;
    previous_camera_x=sample_camera_x;
    previous_camera_y=sample_camera_y;
    previous_aim_x=aim_x;
    previous_aim_y=aim_y;
    output.frames.push_back(record);

    errors.push_back(error_px);
    if (selected) {
      ages.push_back(age_ms);
    }
    if (error_norm < config_.epsilon_enter) {
      seen_below = true;
    }
    if (seen_below) {
      float directionScale = error_norm > 0 ? error_px / error_norm : 0;
      double overshoot = std::max(0.0f, error_px - config_.epsilon_enter * directionScale);
      max_after_below = std::max(max_after_below, overshoot);
    }

    if (config_.occlusion_start >= 0 && !occlusion_recovered &&
        frame >= occlusion_end && valid &&
        error_norm < config_.epsilon_enter) {
      occlusion_recovered = true;
      occlusion_recovery_frame = frame;
    }

    if (frame >= config_.success_after_frame && valid &&
        error_norm < config_.epsilon_enter) {
      ++settle_count;
      if (settle_count >= config_.settle_frames && output.result == TaskResult::Running) {
        output.result = TaskResult::Success;
        output.terminal_frame = frame;
      }
    } else if (frame < config_.success_after_frame || !valid ||
               error_norm > config_.epsilon_exit) {
      settle_count = 0;
    }

    if (!valid) {
      ++unseen_frames;
      if (unseen_frames >= config_.unknown_grace_frames && output.result == TaskResult::Running) {
        output.result = TaskResult::Unknown;
        output.cause = (!connected || !dispatch_ok || feedback_fault || !encoder_valid) ? Cause::Control : Cause::Detection;
        output.terminal_frame = frame;
      }
    } else {
      unseen_frames = 0;
    }
  }

  const auto end_time=std::llround((config_.frames-1)*config_.dt_s*1e9);
  for(const auto& c:transit) event("window_end_pending",c,end_time,0,0);
  if(active_command.id) event("window_end_active",active_command,end_time,0,0);

  if (output.result == TaskResult::Running) {
    output.result = TaskResult::Failed;
    output.terminal_frame = static_cast<int>(output.frames.size()) - 1;
    if (confirmed_frames == 0) {
      output.cause = Cause::Detection;
    } else if (saturated_frames > config_.frames / 2) {
      output.cause = Cause::Control;
    } else if (id_switches > 0) {
      output.cause = Cause::Association;
    } else {
      output.cause = Cause::Timeout;
    }
  }

  output.metrics.frames = static_cast<int>(output.frames.size());
  output.metrics.mae_px = 0;
  if (!errors.empty()) {
    double sum = 0;
    for (double error : errors) sum += error;
    output.metrics.mae_px = sum / errors.size();
  }
  double estimate_sum = 0, prediction_sum = 0;
  int prediction_opportunities = 0;
  for (const auto& record : output.frames) {
    const bool finite=std::isfinite(record.error_norm);
    if(record.frame>=config_.success_after_frame && record.control_valid && finite && record.error_norm<config_.epsilon_enter)
      ++output.metrics.end_stable_frames;
    else output.metrics.end_stable_frames=0;
    if(output.result==TaskResult::Success && record.frame>output.terminal_frame) {
      ++output.metrics.post_success_frames;
      if(!record.control_valid || !finite || record.error_norm>config_.epsilon_exit) {
        ++output.metrics.post_success_violations;
        if(output.metrics.first_post_success_violation<0) output.metrics.first_post_success_violation=record.frame;
      }
    }
    if (record.has_track) {
      ++output.metrics.tracked_frames;
      estimate_sum += std::hypot(record.est_x-record.truth_x, record.est_y-record.truth_y);
    }
    double query = record.frame + config_.command_horizon_s/config_.dt_s;
    if (query > output.frames.size()-1) continue;
    ++prediction_opportunities;
    if (!record.has_track) continue;
    auto lo = static_cast<size_t>(std::floor(query));
    auto hi = std::min(lo+1, output.frames.size()-1);
    double alpha = query-lo;
    double x = (1-alpha)*output.frames[lo].truth_x+alpha*output.frames[hi].truth_x;
    double y = (1-alpha)*output.frames[lo].truth_y+alpha*output.frames[hi].truth_y;
    prediction_sum += std::hypot(record.pred_x-x,record.pred_y-y);
    ++output.metrics.prediction_samples;
  }
  auto& metrics = output.metrics;
  metrics.end_stable=metrics.end_stable_frames>=config_.settle_frames;
  // Require observed maintenance evidence, not a success on the final frame.
  metrics.success_maintained=output.result==TaskResult::Success && metrics.post_success_frames>=config_.settle_frames &&
      metrics.post_success_violations==0;
  metrics.track_coverage = double(metrics.tracked_frames)/config_.frames;
  if (metrics.tracked_frames) metrics.estimate_mae_px = estimate_sum/metrics.tracked_frames;
  if (metrics.prediction_samples) metrics.prediction_mae_px = prediction_sum/metrics.prediction_samples;
  if (prediction_opportunities) metrics.prediction_coverage =
      double(metrics.prediction_samples)/prediction_opportunities;
  output.metrics.p95_px = percentile(errors, 0.95);
  output.metrics.settling_s =
      output.result == TaskResult::Success ? output.terminal_frame * config_.dt_s : -1;
  output.metrics.overshoot_px =
      seen_below ? max_after_below : 0;
  output.metrics.occlusion_recovery_s =
      occlusion_recovered
          ? (occlusion_recovery_frame - occlusion_end) * config_.dt_s
          : -1;
  output.metrics.age_p50_ms = percentile(ages, 0.50);
  output.metrics.age_p95_ms = percentile(ages, 0.95);
  output.metrics.age_p99_ms = percentile(ages, 0.99);
  output.metrics.detections = total_detections;
  output.metrics.false_positives = total_false_positives;
  output.metrics.missed = missed;
  output.metrics.id_switches = id_switches;
  if(config_.feedback_transport) output.feedback_messages=feedback_channel.traces();
  return output;
}

}  // namespace sim
