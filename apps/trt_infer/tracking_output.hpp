#include "tracker.hpp"
#include <ostream>
#include <optional>

inline void writeTracking(std::ostream& out,const mot::Snapshot& s,const mot::Selection& selection) {
    out<<",\"tracks\":[";
    bool first=true;
    for(const auto& t:s.tracks) {
        if(!first) out<<",";
        first=false;
        auto b=t.box();
        out<<"{\"id\":"<<t.id<<",\"state\":\""<<mot::name(t.state)<<"\",\"xyxy\":["
           <<b.x<<","<<b.y<<","<<b.br().x<<","<<b.br().y<<"],\"velocity\":["<<t.x[2]<<","<<t.x[3]
           <<"],\"last_observed_ts_ns\":"<<t.lastSeen<<",\"score\":"<<t.score
           <<",\"association_cost\":"<<t.cost<<",\"mahalanobis_squared\":"<<t.innovation
           <<",\"identity_ambiguous\":"<<(t.ambiguous?"true":"false")<<",\"covariance\":[";
        for(int i=0;i<16;++i) out<<(i?",":"")<<t.covariance.val[i];
        out<<"]";
        if(t.imm) {
            out<<",\"mode_probabilities\":[";
            for(size_t j=0;j<3;++j) out<<(j?",":"")<<t.imm->probabilities[j];
            out<<"]";
        }
        out<<"}";
    }
    out<<"],\"removed_ids\":[";
    for(size_t i=0;i<s.removed.size();++i) out<<(i?",":"")<<s.removed[i];
    out<<"],\"selection\":{\"state\":\""<<selection.state(s)<<"\",\"id\":";
    if(selection.selected) out<<*selection.selected; else out<<"null";
    out<<"}";
}
