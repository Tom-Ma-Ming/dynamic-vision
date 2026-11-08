#pragma once
#include "FeedbackReceiver.hpp"
#include <random>
#include <vector>
#include <string>

namespace sim {
struct FeedbackTrace {
  FeedbackSnapshot snapshot;
  SimTime ready=-1,received=-1;
  std::string status="pending";
};
// Deterministic bounded transport; trace is finite-scenario evidence, not its queue.
class FeedbackChannel {
 public:
  FeedbackChannel(double prior,SimTime delay,SimTime jitter,std::size_t capacity,unsigned seed)
      : receiver_(prior),delay_(delay),jitter_(jitter),capacity_(capacity),rng_(seed) {
    if(delay<0 || jitter<0 || !capacity) throw std::invalid_argument("Invalid feedback channel");
    receiver_.beginEpoch(1,0);
  }
  void event(const std::string& kind,const IncrementCommand& command,SimTime time,double dx,double dy,bool dropped) {
    if(kind=="generated") {
      receiver_.issued(command.id,command.generated,dx,dy);
      FeedbackSnapshot m;m.epoch=receiver_.epoch();m.command_id=command.id;
      snapshots_[command.id]=m;return;
    }
    const bool remote=kind=="accepted" || kind=="applied" || kind=="expired" || kind=="replaced" || kind=="rejected";
    if(!remote) {receiver_.retireLocal(command.id);snapshots_.erase(command.id);return;}
    auto& m=snapshots_.at(command.id);++m.sequence;m.reported=time;
    if(kind=="accepted") m.accepted_at=time;
    else if(kind=="applied") {m.applied_x+=dx;m.applied_y+=dy;}
    else m.terminal=true;
    FeedbackTrace trace;trace.snapshot=m;
    if(dropped) trace.status="dropped";
    else {
      const auto jitter=std::uniform_int_distribution<SimTime>(-jitter_,jitter_)(rng_);
      trace.ready=time+std::max<SimTime>(0,delay_+jitter);
      if(queue_.size()==capacity_) {
        auto oldest=std::min_element(queue_.begin(),queue_.end(),[&](auto a,auto b){return traces_[a].snapshot.reported<traces_[b].snapshot.reported;});
        traces_[*oldest].status="overflow";queue_.erase(oldest);++overflow_;
      }
      queue_.push_back(traces_.size());
    }
    traces_.push_back(trace);
    if(m.terminal) snapshots_.erase(command.id);
  }
  void deliver(SimTime now) {
    std::stable_sort(queue_.begin(),queue_.end(),[&](auto a,auto b){return traces_[a].ready<traces_[b].ready;});
    auto end=queue_.begin();
    while(end!=queue_.end() && traces_[*end].ready<=now) {
      auto& trace=traces_[*end];trace.received=now;
      const auto status=receiver_.receive(trace.snapshot,now);
      if(status==FeedbackDisposition::Applied) {trace.status="applied";++delivered_;}
      else if(status==FeedbackDisposition::Invalid) {trace.status="invalid";++invalid_;}
      else {trace.status=status==FeedbackDisposition::Stale?"stale":"unknown";++ignored_;}
      ++end;
    }
    queue_.erase(queue_.begin(),end);
  }
  void newEpoch(SimTime now) {receiver_.beginEpoch(receiver_.epoch()+1,now);}
  bool overdue(SimTime now,SimTime timeout) const {return receiver_.overdue(now,timeout);}
  bool full() const {return receiver_.size()>=512;}
  auto pending() const {return receiver_.pending();}
  double delay() const {return receiver_.delay();}
  std::size_t outstanding() const {return receiver_.size();}
  std::size_t queued() const {return queue_.size();}
  std::uint64_t epoch() const {return receiver_.epoch();}
  int delivered() const {return delivered_;}
  int ignored() const {return ignored_;}
  int invalid() const {return invalid_;}
  int overflow() const {return overflow_;}
  const auto& traces() const {return traces_;}
 private:
  FeedbackReceiver receiver_;
  SimTime delay_,jitter_;
  std::size_t capacity_;
  std::mt19937 rng_;
  std::map<std::uint64_t,FeedbackSnapshot> snapshots_;
  std::vector<std::size_t> queue_;
  std::vector<FeedbackTrace> traces_;
  int delivered_=0,ignored_=0,invalid_=0,overflow_=0;
};
}
