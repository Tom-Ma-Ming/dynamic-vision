#include "tracker.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
using namespace mot;

void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
Detection person(float x,float y=30) {return {1,.9f,{x-5,y-10,10,20}};}
Detection head(float x,float y=30) {return {0,.9f,{x-2,y-10,4,4}};}

int main(int argc,char** argv) try {
    Config cfg=Config::load(argv[1]);
    constexpr Time step=40000000;
    std::ofstream evidence;
    if(argc>2) { evidence.exceptions(std::ios::failbit|std::ios::badbit); evidence.open(argv[2]); }
    for(int count:{1,2,5,10}) {
        Tracker tracker(cfg);
        Selection selection;
        for(int i=0;i<36;++i) {
            std::vector<Detection> ds;
            for(int j=0;j<count;++j) {
                float x=j==1 ? 111-2*i : 40+2*i+40*(j>1?j:0);
                float y=30+float(j)*3;
                if(j==0 && (i==22 || i==23)) continue;
                ds.push_back(head(x,y));
                if(!(j==0 && i==12)) ds.push_back(person(x,y));
            }
            if(i%2==1) std::reverse(ds.begin(),ds.end());
            require(tracker.update(i,step*i,ds),"update");
            const auto& s=tracker.snapshot();
            require(s.tracks.size()==count,"duplicate or missing track");
            if(argc>2 && count==2) {
                evidence<<"{\"frame_id\":"<<i<<",\"gt\":[";
                for(int j=0;j<count;++j) {
                    auto b=person(j==1?111-2*i:40+2*i,30+3*j).box;
                    evidence<<(j?",":"")<<"["<<j+1<<","<<b.x<<","<<b.y<<","<<b.br().x<<","<<b.br().y<<"]";
                }
                evidence<<"],\"tracks\":[";
                bool first=true;
                for(auto& t:s.tracks) if(t.state==State::Confirmed) {
                    auto b=t.box();
                    evidence<<(first?"":",")<<"["<<t.id<<","<<b.x<<","<<b.y<<","<<b.br().x<<","<<b.br().y<<"]";
                    first=false;
                }
                evidence<<"]}\n";
            }
            if(i==2) require(selection.select(1,s),"select confirmed");
            if(i>=2) {
                for(int j=0;j<count;++j) {
                    const auto& t=s.tracks.at(j);
                    double target=j==1 ? 111-2*i : 40+2*i+40*(j>1?j:0);
                    require(t.id==j+1 && std::abs(t.x[0]-target)<3,"identity or position error");
                }
            }
            if(i==22 || i==23) require(std::string(selection.state(s))=="TemporarilyLost","hold on occlusion");
            if(i==24) require(std::string(selection.state(s))=="Selected","recovery");
        }
        auto prior=tracker.snapshot();
        require(!tracker.update(34,step*36,{}),"stale frame");
        const auto afterStaleFrame=tracker.snapshot();
        require(afterStaleFrame.frame==prior.frame && afterStaleFrame.timestamp==prior.timestamp &&
                afterStaleFrame.tracks.size()==prior.tracks.size(),"stale frame mutated state");
        require(!tracker.update(36,step*34,{}),"stale time");
        const auto afterStaleTime=tracker.snapshot();
        require(afterStaleTime.frame==prior.frame && afterStaleTime.timestamp==prior.timestamp &&
                afterStaleTime.tracks.size()==prior.tracks.size(),"stale time mutated state");
        auto p=tracker.predictAt(step*37);
        require(p.tracks[0].covariance(0,0)>prior.tracks[0].covariance(0,0),"uncertainty");
        require(tracker.snapshot().timestamp==prior.timestamp,"predict mutated state");
        if(count>1) require(selection.select(2,prior),"manual switch");
        tracker.update(70,step*70,{});
        require(tracker.snapshot().removed.size()==count,"expire on long gap");
        require(std::string(selection.state(tracker.snapshot()))=="Unavailable","selection expiry");
    }
    Tracker single(cfg);
    auto weak=person(20); weak.score=.1f;
    single.update(0,0,{weak});
    require(single.snapshot().tracks.empty(),"weak birth");
    single.update(1,step,{head(20)});
    single.update(2,step*2,{head(20),person(20)});
    require(single.snapshot().tracks.size()==1 && single.snapshot().tracks[0].body,"head to body");

    auto high=person(100); high.score=.9f;
    auto low=person(100); low.score=.2f;
    Config byteCfg=cfg; byteCfg.byteTrack=true; byteCfg.highScore=.4; byteCfg.lowScore=.1;
    Tracker baseline(cfg), byte(byteCfg);
    baseline.update(0,0,{high}); byte.update(0,0,{high});
    require(baseline.snapshot().tracks.size()==1 && byte.snapshot().tracks.size()==1,"high birth");
    baseline.update(1,step,{low}); byte.update(1,step,{low});
    require(baseline.snapshot().tracks.size()==1,"baseline low association");
    require(byte.snapshot().tracks.empty(),"bytetrack low must not confirm tentative");
    Tracker confirmed(byteCfg);
    confirmed.update(0,0,{high}); confirmed.update(1,step,{high});
    require(confirmed.snapshot().tracks.size()==1 &&
            confirmed.snapshot().tracks[0].state==State::Confirmed,"confirmed setup");
    confirmed.update(2,step*2,{low});
    require(confirmed.snapshot().tracks.size()==1 &&
            confirmed.snapshot().tracks[0].state==State::Confirmed,"low second association");
    require(confirmed.snapshot().tracks[0].lastSeen==step*2,"low second association timestamp");
    Tracker lowOnly(byteCfg); lowOnly.update(0,0,{low});
    require(lowOnly.snapshot().tracks.empty(),"low must not birth");
    std::cout<<"Synthetic sequences passed: 1/2/5/10 people, crossing, head-only, occlusion, selection, stale results, bytetrack\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n"; return 1;}

