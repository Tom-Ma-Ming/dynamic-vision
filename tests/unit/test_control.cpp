#include "vision/sim/Control.hpp"
#include <iostream>
static void require(bool ok) { if(!ok) throw std::runtime_error("control contract failed"); }
int main() try {
  sim::IncrementActuator a(100);
  require(a.submit({1,0,1,0,10000000,50000000,100,0}));
  require(a.advance(5000000).dx==0);
  require(a.advance(20000000).dx==1);
  require(a.advance(60000000).dx==3);
  require(a.advance(70000000).dx==0);
  require(!a.submit({1,0,1,70000000,70000000,90000000,9,0}));
  require(a.submit({2,1,1,70000000,70000000,90000000,-9,0}));
  require(a.advance(80000000).dx==-1);
  a.cancel(); require(a.advance(90000000).dx==0);
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
