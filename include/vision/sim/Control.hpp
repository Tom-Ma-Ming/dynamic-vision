#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <map>
#include <stdexcept>

namespace sim {
using SimTime = std::int64_t;
struct IncrementCommand {
  std::uint64_t id=0, frame_id=0, track_id=0;
  SimTime generated=0, arrival=0, expires=0;
  double dx=0, dy=0; // actuator units, not image pixels
};
struct AppliedIncrement {
  std::uint64_t command_id=0;
  SimTime timestamp=0;
  double dx=0, dy=0;
};

// Deterministic single-owner component. Caller advances before submit/cancel.
class IncrementActuator {
 public:
  explicit IncrementActuator(double speed) : speed_(speed) {
    if(!std::isfinite(speed) || speed<=0) throw std::invalid_argument("Invalid actuator speed");
  }
  bool submit(const IncrementCommand& c) {
    if(c.id<=last_id_ || c.generated<0 || c.generated>now_ || c.arrival<now_ || c.arrival<c.generated ||
       c.expires<=c.arrival || !std::isfinite(c.dx) || !std::isfinite(c.dy) ||
       !std::isfinite(std::hypot(c.dx,c.dy))) return false;
    if(active_ && active_->track_id!=c.track_id) carry_x_=carry_y_=0;
    active_=c; last_id_=c.id;
    return true;
  }
  void cancel() { active_.reset(); carry_x_=carry_y_=0; }
  AppliedIncrement advance(SimTime time) {
    if(time<now_) throw std::invalid_argument("Simulation time moved backwards");
    AppliedIncrement result{0,time,0,0};
    if(active_) {
      auto& c=*active_;
      result.command_id=c.id;
      const auto start=std::max(now_,c.arrival);
      const auto end=std::min(time,c.expires);
      if(end>start) {
        const double remaining=std::hypot(c.dx,c.dy);
        const double distance=std::min(remaining,speed_*((end-start)*1e-9));
        const double ratio=remaining>0 ? distance/remaining : 0;
        const double x=c.dx*ratio, y=c.dy*ratio;
        c.dx-=x; c.dy-=y;
        carry_x_+=x; carry_y_+=y;
        result.dx=std::trunc(carry_x_); result.dy=std::trunc(carry_y_);
        carry_x_-=result.dx; carry_y_-=result.dy;
      }
      if(time>=c.expires) cancel();
    }
    now_=time;
    return result;
  }
 private:
  double speed_, carry_x_=0, carry_y_=0;
  SimTime now_=0;
  std::uint64_t last_id_=0;
  std::optional<IncrementCommand> active_;
};

// Controller-side ledger: only submitted requests and received feedback enter here.
class CommandFeedback {
 public:
  explicit CommandFeedback(double initial_delay_s):delay_(initial_delay_s) {
    if(!std::isfinite(delay_) || delay_<0) throw std::invalid_argument("Invalid delay prior");
  }
  void issued(std::uint64_t id,SimTime generated,double dx,double dy) {
    if(!id || pending_.count(id) || generated<0 || !std::isfinite(dx) || !std::isfinite(dy))
      throw std::invalid_argument("Invalid issued command");
    pending_[id]={generated,dx,dy,false};
  }
  void acknowledged(std::uint64_t id,SimTime time) {
    auto it=pending_.find(id);
    if(it==pending_.end() || time<it->second.generated) throw std::invalid_argument("Invalid feedback");
    if(it->second.acknowledged) return;
    delay_=.8*delay_+.2*((time-it->second.generated)*1e-9);
    it->second.acknowledged=true;
  }
  void applied(std::uint64_t id,double dx,double dy) {
    auto it=pending_.find(id);
    if(it==pending_.end() || !std::isfinite(dx) || !std::isfinite(dy)) throw std::invalid_argument("Unknown application");
    it->second.x-=dx;it->second.y-=dy;
  }
  void retire(std::uint64_t id) { pending_.erase(id); }
  bool overdue(SimTime now,SimTime timeout) const {
    if(now<0 || timeout<=0) throw std::invalid_argument("Invalid feedback deadline");
    for(const auto& item:pending_)
      if(now>=item.second.generated && now-item.second.generated>=timeout) return true;
    return false;
  }
  void clear() { pending_.clear(); }
  std::size_t size() const { return pending_.size(); }
  double delay() const { return delay_; }
  std::pair<double,double> pending() const {
    std::pair<double,double> sum{};
    for(const auto& item:pending_) {sum.first+=item.second.x;sum.second+=item.second.y;}
    return sum;
  }
 private:
  struct Entry { SimTime generated;double x,y;bool acknowledged; };
  std::map<std::uint64_t,Entry> pending_;
  double delay_;
};

// Gains use seconds; output is actuator units/second after coordinate calibration.
class PidAxis {
 public:
  PidAxis(double kp,double ki,double kd,double integral_limit,double output_limit)
      : kp_(kp),ki_(ki),kd_(kd),ilim_(integral_limit),olim_(output_limit) {
    for(double x:{kp,ki,kd,integral_limit,output_limit})
      if(!std::isfinite(x) || x<0) throw std::invalid_argument("Invalid PID configuration");
    if(output_limit==0) throw std::invalid_argument("Zero PID output limit");
  }
  void reset() { integral_=previous_=0; initialized_=false; }
  double step(double error,double dt) {
    if(!std::isfinite(error) || !std::isfinite(dt) || dt<=0)
      throw std::invalid_argument("Invalid PID sample");
    const double integral=std::clamp(integral_+error*dt,-ilim_,ilim_);
    const double derivative=initialized_ ? (error-previous_)/dt : 0;
    const double raw=kp_*error+ki_*integral+kd_*derivative;
    if(!std::isfinite(raw)) throw std::invalid_argument("PID overflow");
    // Conditional integration avoids winding up further into saturation.
    if(std::abs(raw)<=olim_ || (raw>olim_ && error<0) || (raw<-olim_ && error>0))
      integral_=integral;
    previous_=error; initialized_=true;
    return std::clamp(raw,-olim_,olim_);
  }
 private:
  double kp_,ki_,kd_,ilim_,olim_,integral_=0,previous_=0;
  bool initialized_=false;
};
}
