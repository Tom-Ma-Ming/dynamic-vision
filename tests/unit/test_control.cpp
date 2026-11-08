#include "vision/sim/Control.hpp"
#include "vision/sim/FeedbackReceiver.hpp"
#include "vision/sim/FeedbackChannel.hpp"
#include "vision/sim/StopConfirmation.hpp"
#include <iostream>
#include <limits>
static void require(bool ok) { if(!ok) throw std::runtime_error("control contract failed"); }
int main() try {
  sim::IncrementActuator a(100);
  require(a.idle());
  require(a.submit({1,0,1,0,10000000,50000000,100,0}));
  require(!a.idle());
  require(a.advance(5000000).dx==0);
  require(a.advance(20000000).dx==1);
  require(a.advance(60000000).dx==3);
  require(a.advance(70000000).dx==0);
  require(!a.submit({1,0,1,70000000,70000000,90000000,9,0}));
  require(a.submit({2,1,1,70000000,70000000,90000000,-9,0}));
  require(a.advance(80000000).dx==-1);
  a.cancel(); require(a.advance(90000000).dx==0);
  require(a.idle());
  require(a.submit({3,2,1,90000000,90000000,200000000,10,0}));
  require(a.advance(95000000).dx==0);
  require(a.advance(100000000).dx==1);
  require(a.submit({4,3,1,100000000,110000000,200000000,0,10}));
  require(a.advance(110000000).dx==0);
  auto applied=a.advance(120000000); require(applied.command_id==4 && applied.dy==1 && applied.dx==0);
  bool rejected=false;try { a.advance(1); } catch(const std::invalid_argument&) { rejected=true; }
  require(rejected);
  sim::IncrementActuator small(100);
  double total=0;
  for(int i=0;i<4;++i) {
    auto t=std::int64_t(i)*10000000;
    require(small.submit({std::uint64_t(i+1),0,1,t,t,t+20000000,.25,0}));
    total+=small.advance(t+10000000).dx;
  }
  require(total==1);
  sim::CommandFeedback ledger(.08);
  ledger.issued(1,0,10,-5);
  require(ledger.delay()==.08 && ledger.pending().first==10);
  ledger.acknowledged(1,100000000);
  require(std::abs(ledger.delay()-.084)<1e-12);
  ledger.acknowledged(1,110000000);
  require(std::abs(ledger.delay()-.084)<1e-12);
  ledger.applied(1,4,-2);require(ledger.pending().first==6 && ledger.pending().second==-3);
  ledger.retire(1);require(ledger.pending().first==0);
  ledger.issued(2,100,5,0);
  require(!ledger.overdue(199,100) && ledger.overdue(200,100));
  ledger.acknowledged(2,150);
  require(ledger.overdue(200,100)); // receipt is not execution completion
  ledger.clear();require(ledger.size()==0 && !ledger.overdue(300,100));
  sim::FeedbackReceiver receiver(.08,2);
  receiver.beginEpoch(1,0);receiver.issued(1,0,10,-5);
  sim::FeedbackSnapshot snapshot{1,1,2,120000000,100000000,4,-2,false};
  require(receiver.receive(snapshot,150000000)==sim::FeedbackDisposition::Applied);
  require(receiver.pending().first==6 && std::abs(receiver.delay()-.084)<1e-12);
  require(receiver.receive(snapshot,160000000)==sim::FeedbackDisposition::Stale);
  auto old=snapshot;old.sequence=1;old.applied_x=0;
  require(receiver.receive(old,170000000)==sim::FeedbackDisposition::Stale && receiver.pending().first==6);
  auto invalid=snapshot;invalid.sequence=3;invalid.accepted_at=110000000;
  require(receiver.receive(invalid,170000000)==sim::FeedbackDisposition::Invalid);
  invalid=snapshot;invalid.sequence=3;invalid.reported=200000000;
  require(receiver.receive(invalid,170000000)==sim::FeedbackDisposition::Invalid && receiver.pending().first==6);
  invalid=snapshot;invalid.sequence=3;invalid.applied_x=std::numeric_limits<double>::quiet_NaN();
  require(receiver.receive(invalid,170000000)==sim::FeedbackDisposition::Invalid && receiver.pending().first==6);
  invalid=snapshot;invalid.sequence=3;
  require(receiver.receive(invalid,140000000)==sim::FeedbackDisposition::Invalid && receiver.pending().first==6);
  snapshot.sequence=4;snapshot.reported=180000000;snapshot.applied_x=7;
  require(receiver.receive(snapshot,200000000)==sim::FeedbackDisposition::Applied && receiver.pending().first==3);
  snapshot.sequence=5;snapshot.terminal=true;
  require(receiver.receive(snapshot,210000000)==sim::FeedbackDisposition::Applied && receiver.size()==0);
  require(receiver.receive(snapshot,220000000)==sim::FeedbackDisposition::Unknown);
  receiver.beginEpoch(2,300000000);receiver.issued(1,300000000,20,0);
  require(receiver.receive(snapshot,310000000)==sim::FeedbackDisposition::Stale && receiver.pending().first==20);
  receiver.issued(2,300000000,10,0);
  bool full=false;try {receiver.issued(3,300000000,1,0);} catch(const std::invalid_argument&) {full=true;}
  require(full && receiver.size()==2);
  receiver.retireLocal(1);receiver.retireLocal(2);require(receiver.size()==0);
  sim::FeedbackChannel channel(.08,100,0,2,7);
  sim::IncrementCommand command{1,0,1,0,10,30,10,0};
  channel.event("generated",command,0,10,0,false);
  channel.event("accepted",command,10,0,0,false);
  channel.event("applied",command,20,4,0,false);
  channel.event("expired",command,30,0,0,false);
  require(channel.queued()==2 && channel.overflow()==1);
  channel.deliver(119);require(channel.pending().first==10);
  channel.deliver(120);require(channel.pending().first==6);
  channel.deliver(130);require(channel.outstanding()==0 && channel.invalid()==0);
  command.id=2;command.generated=140;command.arrival=150;command.expires=170;
  channel.event("generated",command,140,10,0,false);
  channel.event("accepted",command,150,0,0,false);
  channel.event("cancelled",command,160,0,0,false);
  channel.newEpoch(160);channel.deliver(250);
  require(channel.ignored()==1 && channel.outstanding()==0);
  sim::StopConfirmation stop;
  auto nonce=stop.request(2,100,50);
  sim::StopSnapshot stopped{2,nonce,110,true};
  require(!stop.accept(stopped,109,2,true)); // future report
  require(!stop.accept(stopped,120,3,true)); // changed session
  require(!stop.accept(stopped,120,2,false)); // executing again
  auto wrong=stopped;wrong.nonce++;
  require(!stop.accept(wrong,120,2,true));
  wrong=stopped;wrong.idle=false;
  require(!stop.accept(wrong,120,2,true));
  require(!stop.accept(stopped,150,2,true)); // deadline is exclusive
  auto fresh=stop.request(2,150,50);
  require(!stop.accept(stopped,160,2,true)); // old nonce
  require(stop.accept({2,fresh,150,true},160,2,true));
  require(!stop.accept({2,fresh,150,true},170,2,true)); // duplicate
  sim::PidAxis slow(0,1,0,100,100), fast(0,1,0,100,100);
  double s=0,f=0;
  for(int i=0;i<10;++i)s=slow.step(1,.1);
  for(int i=0;i<100;++i)f=fast.step(1,.01);
  require(std::abs(s-f)<1e-12);
  sim::PidAxis derivative(0,0,1,0,1000);
  require(derivative.step(10,.01)==0);
  require(derivative.step(11,.01)==100);
  derivative.reset();require(derivative.step(-10,.01)==0);
  std::cout<<"Control contracts passed\n";return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
