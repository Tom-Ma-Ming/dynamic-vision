#include "vision/sim/Simulator.hpp"
#include "vision/sim/Control.hpp"
#include "vision/sim/StopConfirmation.hpp"
#include "vision/sim/EncoderFeedback.hpp"

#include <algorithm>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <deque>
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
  mot::Tracker tracker(tracker_config);
  mot::Selection selection;

  float world_x = config_.start_x;
  float world_y = config_.start_y;
  float vx = config_.vx;
  float vy = config_.vy;
  float height = config_.target_h;
  float bottom = world_y + height * 0.5f;
  float camera_x = 0, camera_y = 0;
  float aim_x = config_.width * 0.5f;
  float aim_y = config_.height * 0.5f;
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
    if(config_.incremental_control) {
      std::stable_sort(transit.begin(),transit.end(),[](const auto& a,const auto& b){return a.arrival<b.arrival;});
      while((!transit.empty() && transit.front().arrival<=ts_ns) ||
            (stop_request_ready>=0 && stop_request_ready<=ts_ns)) {
        // Stop wins ties and cancels older in-flight commands at the device barrier.
        if(stop_request_ready>=0 && stop_request_ready<=ts_ns &&
           (transit.empty() || stop_request_ready<=transit.front().arrival)) {
          const auto time=stop_request_ready;stop_request_ready=-1;
          if(connected) {
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
      apply(ts_ns);
      if(config_.feedback_transport) feedback_channel.deliver(ts_ns);
      const bool feedback_overdue=config_.feedback_timeout_ms>0 && (config_.feedback_transport ?
        feedback_channel.overdue(ts_ns,std::llround(config_.feedback_timeout_ms*1e6)) :
        feedback.overdue(ts_ns,std::llround(config_.feedback_timeout_ms*1e6)));
      if(feedback_overdue || (config_.feedback_transport && feedback_channel.full())) {
        feedback_capacity_fault=config_.feedback_transport && feedback_channel.full();
        if(!feedback_fault && config_.stop_request_delay_ms>0) {
          stop_request_ready=ts_ns+std::llround(config_.stop_request_delay_ms*1e6);
          stop_request_nonce=0; // automatic stop cannot authorize recovery
        }
        feedback_fault=true;feedback.clear();
        if(config_.feedback_transport) feedback_channel.newEpoch(ts_ns);
      }
      // Explicit recovery request; a received idle snapshot confirms the model
      // has cancelled active work and drained transport. Time alone never clears it.
      if(feedback_fault && !feedback_resynced && config_.feedback_resync_frame>=0 &&
         frame>=config_.feedback_resync_frame && connected) {
        const bool idle_snapshot=actuator.idle() && transit.empty() && active_command.id==0;
        bool confirmed=false;
        if(config_.feedback_transport) {
          // Only one response is retained; expiry permits a fresh nonce.
          // Check an arriving response before issuing a replacement request.
          if(stop_ready>=0 && stop_ready<=ts_ns) {
            confirmed=stop_confirmation.accept(stop_snapshot,ts_ns,feedback_channel.epoch(),idle_snapshot);
            stop_ready=-1;
          }
          if(!confirmed && !stop_confirmation.pending(ts_ns) && stop_request_ready<0) {
            ++feedback_sync_attempts;
            const auto nonce=stop_confirmation.request(feedback_channel.epoch(),ts_ns,
                std::llround(config_.feedback_timeout_ms*1e6));
            stop_ready=-1;
            if(config_.stop_request_delay_ms>0) {
              stop_request_ready=ts_ns+std::llround(config_.stop_request_delay_ms*1e6);
              stop_request_nonce=nonce;stop_request_epoch=feedback_channel.epoch();
            } else {
              replyStop(ts_ns,feedback_channel.epoch(),nonce,idle_snapshot);
              if(stop_ready==ts_ns) {
                confirmed=stop_confirmation.accept(stop_snapshot,ts_ns,feedback_channel.epoch(),idle_snapshot);
                stop_ready=-1;
              }
            }
          }
        } else {
          ++feedback_sync_attempts;
          if(dropFeedback(ts_ns)) ++feedback_dropped;
          else confirmed=idle_snapshot;
        }
        if(confirmed) {
          feedback.clear();pid_x.reset();pid_y.reset();
          if(config_.feedback_transport) feedback_channel.newEpoch(ts_ns);
          feedback_fault=false;feedback_resynced=true;
          feedback_capacity_fault=false;
        }
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

    const float truth_x = config_.view_feedback ? world_x-camera_x : clampf(world_x - camera_x,
                                 config_.target_w * 0.5f,
                                 static_cast<float>(config_.width) - config_.target_w * 0.5f);
    const float truth_y = config_.view_feedback ? world_y-camera_y : clampf(world_y,
                                 config_.target_h * 0.5f,
                                 static_cast<float>(config_.height) - config_.target_h * 0.5f);
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
    while(!observations.empty() && observations.front().ready<=ts_ns) {
      const auto& observation=observations.front();
      auto& trace=output.observations[observation.frame];trace.released=ts_ns;
      if(config_.encoder_transport && !encoder_channel.at(observation.captured)) {
        trace.status="encoder_missing";observations.pop_front();continue;
      }
      if(observation.captured<=tracker.snapshot().timestamp) {
        trace.status="stale";observations.pop_front();continue;
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
      observations.pop_front();
    }
    while(observations.size()>size_t(config_.observation_capacity)) {
      auto oldest=std::min_element(observations.begin(),observations.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
      auto& trace=output.observations[oldest->frame];trace.status="overflow";trace.released=ts_ns;
      observations.erase(oldest);
    }
    const auto received_encoder=encoder_channel.latest();
    const auto extrapolated=config_.encoder_extrapolation ?
        encoder_channel.extrapolatedPosition(config_.actuator_max_speed) : std::optional<std::pair<double,double>>{};
    const bool encoder_valid=!config_.encoder_transport ||
        (received_encoder && observation_frame>=0 && received_encoder->captured>=tracker.snapshot().timestamp &&
         (!config_.encoder_extrapolation || extrapolated.has_value()));
    const double available_x=extrapolated ? extrapolated->first : config_.encoder_transport ? (received_encoder?received_encoder->x:observed_encoder_x) : encoder_x;
    const double available_y=extrapolated ? extrapolated->second : config_.encoder_transport ? (received_encoder?received_encoder->y:observed_encoder_y) : encoder_y;
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
    const double covariance_trace =
        selected ? selected->covariance(0, 0) + selected->covariance(1, 1) : 0;

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
    if (controllable) {
      const float horizon = config_.feedback_prediction ? float(feedbackDelay()) : config_.compensate_actuator_delay
                                ? config_.actuator_delay_frames * dt
                                : config_.command_horizon_s;
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
    if(config_.incremental_control) {
      if(controllable && connected && !feedback_fault && encoder_valid) {
        const double delay=std::max(0.0,config_.actuator_delay_frames*config_.dt_s+jitter(delay_rng)*.001);
        const auto arrival=ts_ns+std::llround(delay*1e9);
        command_lead_ms=(arrival-ts_ns)*1e-6;
        const auto pending=config_.feedback_prediction ? (config_.feedback_transport?feedback_channel.pending():feedback.pending()) : std::pair<double,double>{};
        const double fx=config_.velocity_feedforward ? selected->x[2]*config_.actuator_units_per_px : 0;
        const double fy=config_.velocity_feedforward ? selected->x[3]*config_.actuator_units_per_px : 0;
        const double ux=pid_x.step((target.first-aim_x)*config_.actuator_units_per_px-pending.first,config_.dt_s,fx);
        const double uy=pid_y.step((target.second-aim_y)*config_.actuator_units_per_px-pending.second,config_.dt_s,fy);
        transit.push_back({std::uint64_t(frame+1),std::uint64_t(frame),selected->id,
                          ts_ns,arrival,arrival+std::llround(2*config_.dt_s*1e9),ux*config_.dt_s,uy*config_.dt_s});
        event("generated",transit.back(),ts_ns,ux*config_.dt_s,uy*config_.dt_s);
        if(config_.dispatch_failure_rate>0 && failure(failure_rng)<config_.dispatch_failure_rate) {
          event("dispatch_failed",transit.back(),ts_ns,0,0);transit.pop_back();dispatch_ok=false;
          pid_x.reset();pid_y.reset();
        }
        if(std::hypot(ux,uy)>=config_.actuator_max_speed*config_.actuator_units_per_px) ++saturated_frames;
      } else if(feedback_fault && connected && config_.stop_request_delay_ms>0) {
        // Block generation immediately; physical stop waits for transport.
        pid_x.reset();pid_y.reset();
      } else {
        for(const auto& c:transit) event(connected ? "cancelled_in_transit" : "disconnected_in_transit",c,ts_ns,0,0);
        if(active_command.id) event(connected ? "cancelled" : "disconnected",active_command,ts_ns,0,0);
        active_command={};transit.clear();actuator.cancel();pid_x.reset();pid_y.reset();
      }
    } else {
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
    record.camera_x=camera_x;record.camera_y=camera_y;
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
