#include "vision/sim/StageScheduler.hpp"
#include <iostream>
#include <stdexcept>

int main() {
  bool rollback_rejected=false, dependency_order_valid=false, negative_rejected=false;
  {
    sim::StageScheduler queue(4);
    queue.enqueue(sim::Stage::Execution,100,0);
    const auto first=queue.popDue(100);
    try {
      if(!queue.enqueue(sim::Stage::Execution,50,1)) rollback_rejected=true;
      else {
        const auto second=queue.popDue(100);
        rollback_rejected=!second || second->time_ns>=first->time_ns;
        if(second) std::cout<<"execution_clock="<<first->time_ns<<" -> "<<second->time_ns<<'\n';
      }
    } catch(const std::invalid_argument&) {rollback_rejected=true;}
  }
  {
    sim::StageScheduler queue(4);
    try {
      const bool infer=queue.enqueue(sim::Stage::Inference,10,0);
      const bool capture=queue.enqueue(sim::Stage::Capture,20,0);
      if(!infer || !capture) dependency_order_valid=true;
      else {
        const auto first=queue.popDue(20);
        const auto second=queue.popDue(20);
        dependency_order_valid=!second || (first && second->time_ns>=first->time_ns);
        if(first && second) std::cout<<"dependency_clock="<<first->time_ns<<" -> "<<second->time_ns<<'\n';
      }
    } catch(const std::invalid_argument&) {dependency_order_valid=true;}
  }
  {
    sim::StageScheduler queue(1);
    try {negative_rejected=!queue.enqueue(sim::Stage::Execution,-1,0);}
    catch(const std::invalid_argument&) {negative_rejected=true;}
  }
  std::cout<<std::boolalpha<<"rollback_rejected="<<rollback_rejected
      <<" dependency_order_valid="<<dependency_order_valid<<" negative_rejected="<<negative_rejected<<'\n';
  return rollback_rejected && dependency_order_valid && negative_rejected ? 0 : 1;
}
