#pragma once
#include "Control.hpp"
#include <deque>

namespace sim {
struct EncoderSample {SimTime captured=0;double x=0,y=0;};
// Fixed-delay telemetry in the simulator clock. Both transit and history bounded.
class EncoderFeedback {
 public:
  EncoderFeedback(SimTime delay,SimTime max_age,std::size_t capacity=256)
      :delay_(delay),max_age_(max_age),capacity_(capacity) {
    if(delay<0 || max_age<=0 || !capacity) throw std::invalid_argument("Invalid encoder transport");
  }
  void publish(EncoderSample sample,bool dropped) {
    if(sample.captured<=last_ || !std::isfinite(sample.x) || !std::isfinite(sample.y))
      throw std::invalid_argument("Invalid encoder sample");
    last_=sample.captured;
    if(dropped) {++dropped_;return;}
    if(pending_.size()==capacity_) {pending_.pop_front();++overflow_;}
    pending_.push_back(sample);
  }
  void advance(SimTime now) {
    if(now<now_) throw std::invalid_argument("Encoder clock reversed");
    now_=now;
    while(!pending_.empty() && pending_.front().captured<=now-delay_) {
      if(history_.size()==capacity_) history_.pop_front();
      history_.push_back(pending_.front());pending_.pop_front();
    }
  }
  std::optional<EncoderSample> at(SimTime captured) const {
    for(const auto& s:history_) if(s.captured==captured) return s;
    return {};
  }
  std::optional<EncoderSample> latest() const {
    if(history_.empty() || now_-history_.back().captured>max_age_) return {};
    return history_.back();
  }
  std::optional<std::pair<double,double>> extrapolatedPosition(double max_speed) const {
    if(!std::isfinite(max_speed) || max_speed<=0) throw std::invalid_argument("Invalid encoder speed bound");
    if(!latest() || history_.size()<2) return {};
    const auto& b=history_.back();const auto& a=history_[history_.size()-2];
    if(b.captured-a.captured>max_age_) return {};
    const double dt=(b.captured-a.captured)*1e-9;
    double vx=(b.x-a.x)/dt,vy=(b.y-a.y)/dt;
    const double speed=std::hypot(vx,vy);
    if(!std::isfinite(speed)) return {};
    if(speed>max_speed) {vx*=max_speed/speed;vy*=max_speed/speed;}
    const double horizon=(now_-b.captured)*1e-9;
    return std::pair<double,double>{b.x+vx*horizon,b.y+vy*horizon};
  }
  std::size_t queued() const {return pending_.size();}
  int dropped() const {return dropped_;}
  int overflow() const {return overflow_;}
 private:
  SimTime delay_,max_age_,last_=-1,now_=0;
  std::size_t capacity_;
  std::deque<EncoderSample> pending_,history_;
  int dropped_=0,overflow_=0;
};
}
