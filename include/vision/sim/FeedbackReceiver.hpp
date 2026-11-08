#pragma once
#include "Control.hpp"

namespace sim {
// All times are already translated to the controller's simulation clock.
// Each message is a cumulative command snapshot, not an incremental delta.
struct FeedbackSnapshot {
  std::uint64_t epoch=0, command_id=0, sequence=0;
  SimTime reported=0, accepted_at=-1;
  double applied_x=0, applied_y=0;
  bool terminal=false;
};
enum class FeedbackDisposition { Applied, Stale, Unknown, Invalid };

class FeedbackReceiver {
 public:
  explicit FeedbackReceiver(double delay_prior,std::size_t capacity=512)
      : ledger_(delay_prior),capacity_(capacity) {
    if(!capacity_) throw std::invalid_argument("Zero feedback capacity");
  }
  // Caller must first confirm the actuator stopped and old transport drained.
  // This is not a time-based automatic recovery operation.
  void beginEpoch(std::uint64_t epoch,SimTime time) {
    if(epoch<=epoch_ || time<epoch_start_ || time<last_received_) throw std::invalid_argument("Invalid feedback epoch");
    ledger_.clear();states_.clear();epoch_=epoch;epoch_start_=time;last_id_=0;
  }
  void issued(std::uint64_t id,SimTime generated,double x,double y) {
    if(!epoch_ || id<=last_id_ || generated<epoch_start_ || states_.size()>=capacity_)
      throw std::invalid_argument("Invalid or over-capacity feedback issue");
    ledger_.issued(id,generated,x,y);
    states_.emplace(id,State{generated,x,y});last_id_=id;
  }
  FeedbackDisposition receive(const FeedbackSnapshot& m,SimTime received) {
    if(received<last_received_) return FeedbackDisposition::Invalid;
    if(m.epoch<epoch_) return FeedbackDisposition::Stale;
    if(m.epoch!=epoch_ || !epoch_ || received<epoch_start_ || m.reported>received ||
       !m.sequence || !std::isfinite(m.applied_x) || !std::isfinite(m.applied_y))
      return FeedbackDisposition::Invalid;
    auto found=states_.find(m.command_id);
    if(found==states_.end()) return FeedbackDisposition::Unknown;
    auto& state=found->second;
    if(m.sequence<=state.sequence) return FeedbackDisposition::Stale;
    if(m.reported<state.generated || m.reported<state.reported || m.accepted_at< -1 ||
       (m.accepted_at>=0 && (m.accepted_at<state.generated || m.accepted_at>m.reported)) ||
       (state.accepted_at>=0 && m.accepted_at!=state.accepted_at) ||
       (m.accepted_at<0 && (m.applied_x!=0 || m.applied_y!=0)) ||
       !std::isfinite(state.requested_x-m.applied_x) || !std::isfinite(state.requested_y-m.applied_y))
      return FeedbackDisposition::Invalid;
    const double dx=m.applied_x-state.applied_x,dy=m.applied_y-state.applied_y;
    if(!std::isfinite(dx) || !std::isfinite(dy)) return FeedbackDisposition::Invalid;
    if(m.accepted_at>=0) ledger_.acknowledged(m.command_id,m.accepted_at);
    ledger_.applied(m.command_id,dx,dy);
    if(m.terminal) {ledger_.retire(m.command_id);states_.erase(found);}
    else {
      state.sequence=m.sequence;state.reported=m.reported;state.accepted_at=m.accepted_at;
      state.applied_x=m.applied_x;state.applied_y=m.applied_y;
    }
    last_received_=received;
    return FeedbackDisposition::Applied;
  }
  void retireLocal(std::uint64_t id) {ledger_.retire(id);states_.erase(id);}
  std::pair<double,double> pending() const {return ledger_.pending();}
  double delay() const {return ledger_.delay();}
  bool overdue(SimTime now,SimTime timeout) const {return ledger_.overdue(now,timeout);}
  std::size_t size() const {return states_.size();}
  std::uint64_t epoch() const {return epoch_;}
 private:
  struct State {
    SimTime generated;
    double requested_x,requested_y;
    std::uint64_t sequence=0;
    SimTime reported=-1,accepted_at=-1;
    double applied_x=0,applied_y=0;
  };
  CommandFeedback ledger_;
  std::map<std::uint64_t,State> states_;
  std::size_t capacity_;
  std::uint64_t epoch_=0,last_id_=0;
  SimTime epoch_start_=0;
  SimTime last_received_=-1;
};
}
