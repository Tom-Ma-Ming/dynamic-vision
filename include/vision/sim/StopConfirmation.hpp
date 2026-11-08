#pragma once
#include "Control.hpp"
#include <cstdint>
#include <stdexcept>
#include <limits>

namespace sim {
struct StopSnapshot {
  std::uint64_t epoch=0, nonce=0;
  SimTime reported=0;
  bool idle=false;
};
// One outstanding request. Expired, duplicate and previous-session replies
// cannot release a fault latch. Times share the simulator's monotonic clock.
class StopConfirmation {
 public:
  std::uint64_t request(std::uint64_t epoch,SimTime now,SimTime timeout) {
    if(!epoch || now<requested_ || timeout<=0 || now>std::numeric_limits<SimTime>::max()-timeout ||
       nonce_==std::numeric_limits<std::uint64_t>::max() || pending(now))
      throw std::invalid_argument("Invalid stop confirmation request");
    epoch_=epoch;requested_=now;deadline_=now+timeout;active_=true;
    return ++nonce_;
  }
  bool pending(SimTime now) const {return active_ && now<deadline_;}
  bool accept(const StopSnapshot& message,SimTime now,std::uint64_t epoch,bool idle_now) {
    if(!pending(now) || epoch!=epoch_ || message.epoch!=epoch_ || message.nonce!=nonce_ ||
       message.reported<requested_ || message.reported>now || now<requested_ ||
       !message.idle || !idle_now) return false;
    active_=false;return true;
  }
 private:
  std::uint64_t epoch_=0,nonce_=0;
  SimTime requested_=0,deadline_=0;
  bool active_=false;
};
}
