#include "vision/control/Assist.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vision::serial {

const char* controllerName(ControllerMode mode) {
  switch (mode) {
    case ControllerMode::Off: return "off";
    case ControllerMode::LimitedStep: return "limited_step";
    case ControllerMode::FixedPid: return "fixed_pid";
  }
  return "unknown";
}

ControllerMode parseController(const std::string& name) {
  if (name == "off") return ControllerMode::Off;
  if (name == "limited_step") return ControllerMode::LimitedStep;
  if (name == "fixed_pid") return ControllerMode::FixedPid;
  throw std::invalid_argument("Unknown controller: " + name);
}

AssistController::AssistController(ControlConfig config) : config_(std::move(config)) {
  if (!(config_.exit_px >= config_.enter_px) || config_.enter_px < 0 || config_.deadzone_px < 0 ||
      config_.max_step_px <= 0 || config_.max_speed_px_s <= 0 || config_.max_accel_px_s2 < 0 ||
      config_.counts_per_px <= 0 || config_.max_counts_per_axis <= 0 || config_.firmware_axis_limit <= 0 ||
      config_.kp < 0 || config_.ki < 0 || config_.kd < 0 || config_.integral_limit < 0 ||
      config_.derivative_tau_s < 0 || config_.output_limit_px_s <= 0 || config_.command_lifetime_ns <= 0 ||
      config_.resume_wait_s < 0) {
    throw std::invalid_argument("Invalid assist configuration");
  }
  for (double value : {config_.kp, config_.ki, config_.kd, config_.enter_px, config_.exit_px,
                       config_.deadzone_px, config_.max_step_px, config_.counts_per_px}) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite assist configuration");
  }
}

void AssistController::setMode(ControllerMode mode) {
  if (config_.mode == mode) return;
  config_.mode = mode;
  resetDynamics();
}

void AssistController::newEpoch(std::uint64_t epoch) {
  epoch_ = epoch;
  selected_ = 0;
  hold_ = false;
  latched_ = false;
  resetDynamics();
}

void AssistController::resetDynamics() {
  integral_x_ = integral_y_ = 0;
  filt_x_ = filt_y_ = 0;
  prev_x_ = prev_y_ = 0;
  speed_x_ = speed_y_ = 0;
  frac_x_ = frac_y_ = 0;
  derivative_ready_ = false;
  in_flight_until_ = -1;
}

Decision AssistController::finish(Decision decision, const Observation& observation) {
  decision.controller = controllerName(config_.mode);
  decision.law = decision.controller;
  decision.human_hold = hold_;
  decision.latched = latched_;
  decision.frac_x = frac_x_;
  decision.frac_y = frac_y_;
  decision.selected_track = selected_;
  decision.epoch = epoch_;
  if (!decision.emit) {
    decision.dx_counts = 0;
    decision.dy_counts = 0;
    decision.command_id = 0;
  }
  (void)observation;
  return decision;
}

Decision AssistController::step(const Observation& observation) {
  Decision decision;
  const std::int64_t now = observation.control_ns;
  if (have_clock_ && now <= last_ns_) throw std::invalid_argument("Control clock moved backwards");
  const double error_x = observation.est_x - config_.center_x;
  const double error_y = observation.est_y - config_.center_y;
  decision.error_x = error_x;
  decision.error_y = error_y;
  decision.distance_px = std::hypot(error_x, error_y);
  bool reset = false;

  if (observation.human_explicit) {
    hold_ = true;
    resume_at_ = -1;
    latched_ = false;
    resetDynamics();
    reset = true;
    decision.transition = "human_hold";
  } else if (observation.human_release && hold_) {
    hold_ = false;
    resume_at_ = now + static_cast<std::int64_t>(std::llround(config_.resume_wait_s * 1e9));
    latched_ = false;
    resetDynamics();
    reset = true;
    decision.transition = "human_release";
  } else if (!hold_ && std::hypot(observation.human_dx_px, observation.human_dy_px) >= config_.human_turn_px) {
    resume_at_ = now + static_cast<std::int64_t>(std::llround(config_.resume_wait_s * 1e9));
    latched_ = false;
    resetDynamics();
    reset = true;
    decision.transition = "human_turn";
  }

  const bool waiting = resume_at_ >= 0 && now < resume_at_;
  decision.waiting = waiting;
  bool ready = observation.has_track && observation.track_state == "Confirmed" && !observation.ambiguous &&
               !observation.identity_conflict && observation.track_id != 0 &&
               observation.age_ms <= config_.max_age_ms && observation.covariance <= config_.covariance_limit;
  if (ready && selected_ == 0) selected_ = observation.track_id;
  if (ready && selected_ != 0 && observation.track_id != selected_) {
    ready = false;
    decision.transition = "identity_held";
    latched_ = false;
  }
  decision.ready = ready;
  if (!ready || hold_ || waiting) {
    if (!reset && (hold_ || waiting || !ready)) {
      // Drop accumulated assist state when the gate closes. A later epoch or
      // release must not spend a fraction saved from the previous gate.
      if (latched_ || frac_x_ != 0 || frac_y_ != 0 || integral_x_ != 0 || integral_y_ != 0) {
        resetDynamics();
        reset = true;
      }
    }
    latched_ = false;
  } else if (!latched_ && decision.distance_px <= config_.enter_px) {
    latched_ = true;
    if (decision.transition.empty()) decision.transition = "enter";
  } else if (latched_ && decision.distance_px > config_.exit_px) {
    latched_ = false;
    resetDynamics();
    reset = true;
    decision.transition = "exit";
  }

  decision.integral_reset = reset;
  decision.human_hold = hold_;
  decision.latched = latched_;

  const bool first = !have_clock_;
  double dt = 0;
  if (!first) {
    dt = static_cast<double>(now - last_ns_) * 1e-9;
    if (!std::isfinite(dt) || dt <= 0) throw std::invalid_argument("Invalid control dt");
  }
  last_ns_ = now;
  have_clock_ = true;
  decision.dt_s = dt;
  decision.dt_valid = !first;
  if (first) {
    prev_x_ = error_x;
    prev_y_ = error_y;
    decision.gate = config_.mode == ControllerMode::Off ? "assist_off" : hold_ ? "human_hold" : "need_dt";
    return finish(decision, observation);
  }

  if (config_.mode == ControllerMode::Off) {
    decision.gate = "assist_off";
    prev_x_ = error_x;
    prev_y_ = error_y;
    return finish(decision, observation);
  }
  if (hold_) {
    decision.gate = "human_hold";
    return finish(decision, observation);
  }
  if (waiting) {
    decision.gate = "resume_wait";
    return finish(decision, observation);
  }
  if (!ready) {
    decision.gate = decision.transition == "identity_held" ? "identity_held" : "unreliable";
    return finish(decision, observation);
  }
  if (!latched_) {
    decision.gate = "outside";
    return finish(decision, observation);
  }
  if (config_.max_in_flight > 0 && in_flight_until_ >= 0 && now < in_flight_until_) {
    decision.gate = "in_flight";
    prev_x_ = error_x;
    prev_y_ = error_y;
    return finish(decision, observation);
  }

  auto quantize = [&](double px, double& frac) {
    const int limit = std::min(config_.max_counts_per_axis, config_.firmware_axis_limit);
    const double scaled = px * config_.counts_per_px + frac;
    const double truncated = std::trunc(scaled);
    int counts = static_cast<int>(truncated);
    double remainder = scaled - truncated;
    if (counts > limit || counts < -limit) {
      counts = std::clamp(counts, -limit, limit);
      remainder = 0;
      decision.saturated = true;
    }
    frac = remainder;
    return counts;
  };
  auto clampVector = [&](double& x, double& y, double limit) {
    const double mag = std::hypot(x, y);
    if (mag > limit && mag > 0) {
      x *= limit / mag;
      y *= limit / mag;
      decision.saturated = true;
    }
  };

  double command_x = 0, command_y = 0;
  if (config_.mode == ControllerMode::LimitedStep) {
    const double room = std::max(0.0, decision.distance_px - config_.deadzone_px);
    const double step_cap = std::min(config_.max_step_px, config_.max_speed_px_s * dt);
    const double correction = decision.distance_px > 1e-9 ? std::min(room, step_cap) : 0;
    const double ux = decision.distance_px > 1e-9 ? error_x / decision.distance_px : 0;
    const double uy = decision.distance_px > 1e-9 ? error_y / decision.distance_px : 0;
    double want_x = ux * correction + observation.est_vx * dt;
    double want_y = uy * correction + observation.est_vy * dt;
    clampVector(want_x, want_y, step_cap);
    const double want = std::hypot(want_x, want_y);
    if (room <= 0 && want < 0.05) {
      speed_x_ = speed_y_ = 0;
      frac_x_ = frac_y_ = 0;
      integral_x_ = integral_y_ = 0;
      decision.integral_reset = true;
      decision.gate = "deadzone";
      prev_x_ = error_x;
      prev_y_ = error_y;
      return finish(decision, observation);
    }
    double vx = want_x / dt;
    double vy = want_y / dt;
    double dvx = vx - speed_x_;
    double dvy = vy - speed_y_;
    const double max_dv = config_.max_accel_px_s2 * dt;
    clampVector(dvx, dvy, max_dv);
    speed_x_ += dvx;
    speed_y_ += dvy;
    clampVector(speed_x_, speed_y_, config_.max_speed_px_s);
    command_x = speed_x_ * dt;
    command_y = speed_y_ * dt;
    decision.pre_x_px = command_x;
    decision.pre_y_px = command_y;
    clampVector(command_x, command_y, config_.max_step_px);
    decision.gate = room <= 0 ? "hold_velocity" : "assisting";
  } else {
    double d_x = 0, d_y = 0;
    if (!derivative_ready_) {
      d_x = d_y = 0;
      derivative_ready_ = true;
      filt_x_ = filt_y_ = 0;
    } else {
      const double raw_x = (error_x - prev_x_) / dt;
      const double raw_y = (error_y - prev_y_) / dt;
      const double alpha = dt / (config_.derivative_tau_s + dt);
      filt_x_ += alpha * (raw_x - filt_x_);
      filt_y_ += alpha * (raw_y - filt_y_);
      d_x = filt_x_;
      d_y = filt_y_;
    }
    if (decision.distance_px <= config_.deadzone_px) {
      resetDynamics();
      decision.integral_reset = true;
      decision.gate = "deadzone";
      decision.dt_s = dt;
      decision.dt_valid = true;
      prev_x_ = error_x;
      prev_y_ = error_y;
      return finish(decision, observation);
    }
    auto axis = [&](double error, double& integral, double derivative, double& p, double& i, double& d) {
      const double candidate = std::clamp(integral + error * dt, -config_.integral_limit, config_.integral_limit);
      p = config_.kp * error;
      i = config_.ki * candidate;
      d = config_.kd * derivative;
      const double raw = p + i + d;
      const bool release = std::abs(raw) <= config_.output_limit_px_s || (raw > config_.output_limit_px_s && error < 0) ||
                           (raw < -config_.output_limit_px_s && error > 0);
      if (release) integral = candidate;
      if (std::abs(raw) > config_.output_limit_px_s) decision.saturated = true;
      return std::clamp(raw, -config_.output_limit_px_s, config_.output_limit_px_s) * dt;
    };
    command_x = axis(error_x, integral_x_, d_x, decision.p_x, decision.i_x, decision.d_x);
    command_y = axis(error_y, integral_y_, d_y, decision.p_y, decision.i_y, decision.d_y);
    decision.pre_x_px = command_x;
    decision.pre_y_px = command_y;
    clampVector(command_x, command_y, config_.max_step_px);
    decision.gate = "assisting";
  }

  decision.post_x_px = command_x;
  decision.post_y_px = command_y;
  decision.dx_counts = quantize(command_x, frac_x_);
  decision.dy_counts = quantize(command_y, frac_y_);
  prev_x_ = error_x;
  prev_y_ = error_y;
  if (decision.dx_counts == 0 && decision.dy_counts == 0) {
    decision.emit = false;
    if (decision.gate == "assisting") decision.gate = "fraction_pending";
    return finish(decision, observation);
  }
  decision.emit = true;
  decision.command_id = next_id_++;
  decision.deadline_ns = now + config_.command_lifetime_ns;
  in_flight_until_ = decision.deadline_ns;
  return finish(decision, observation);
}

}  // namespace vision::serial
