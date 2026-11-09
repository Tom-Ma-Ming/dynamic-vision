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

    for(bool useByte:{false,true}) {
      for(bool separate:{false,true}) {
        auto c=cfg;c.byteTrack=useByte;c.separateHeadAmbiguity=separate;
        Tracker measured(c);
        measured.update(0,0,{person(50)});
        require(!measured.snapshot().tracks.at(0).lastHeadSeen,"body-only birth is not a measured head");
        measured.update(1,step,{person(50),head(50)});
        require(measured.snapshot().tracks.at(0).headMeasuredAt(step),"unique head measurement timestamp");
        auto future=measured.predictAt(step+1);
        require(!future.tracks.at(0).headMeasuredAt(step+1),"prediction cannot fabricate fresh head");
        measured.update(2,step*2,{person(50)});
        require(!measured.snapshot().tracks.at(0).headMeasuredAt(step*2) &&
                measured.snapshot().tracks.at(0).lastHeadSeen==step,"body-only update preserves historical head timestamp");
        measured.update(3,step*3,{person(50),head(50),head(51)});
        require(!measured.snapshot().tracks.at(0).headMeasuredAt(step*3) &&
                measured.snapshot().tracks.at(0).lastHeadSeen==step,"conflicting heads cannot refresh valid measurement");
        measured.update(4,step*4,{});
        require(!measured.snapshot().tracks.at(0).headMeasuredAt(step*4),"lost track cannot claim fresh head");
        measured.update(5,step*5,{person(50),head(50)});
        require(measured.snapshot().tracks.at(0).headMeasuredAt(step*5),"unique head recovery");
        if(useByte) {
            auto lowBody=person(50);lowBody.score=.2f;
            measured.update(6,step*6,{lowBody,head(50)});
            require(measured.snapshot().tracks.at(0).headMeasuredAt(step*6),"ByteTrack second-stage head measurement");
        }
        Tracker headOnly(c);headOnly.update(0,0,{head(50)});
        require(headOnly.snapshot().tracks.at(0).headMeasuredAt(0),"head birth at zero timestamp");
        Tracker shared(c);shared.update(0,0,{person(50),person(52),head(51)});
        for(const auto& t:shared.snapshot().tracks)
            require(!t.lastHeadSeen,"head shared between bodies is not reliable measurement");
      }
      auto bodyCfg=cfg;bodyCfg.separateHeadAmbiguity=true;bodyCfg.byteTrack=useByte;
      Tracker bodyPrimary(bodyCfg);
      for(int i=0;i<3;++i) bodyPrimary.update(i,step*i,{person(50),head(50),head(51)});
      const auto t=bodyPrimary.snapshot().tracks.at(0);
      require(t.body && !t.ambiguous && t.headAmbiguous,"head conflict must not invalidate unambiguous body");
      bodyPrimary.update(3,step*3,{head(50)});
      require(bodyPrimary.snapshot().tracks.at(0).lastSeen==step*2,"conflicted head anchor cannot match head-only detection");
    }
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
    for(bool enabled : {false,true}) {
        for(float lastScore : {.9f,.2f}) {
            auto config=cfg; config.byteTrack=enabled;
            Tracker overlap(config);
            overlap.update(0,0,{person(100),person(102)});
            overlap.update(1,step,{person(100),person(102)});
            auto merged=person(101); merged.score=lastScore;
            overlap.update(2,step*2,{merged,merged});
            require(overlap.snapshot().tracks.size()==2,"overlap preserves two IDs");
            Selection focus;
            for(const auto& t:overlap.snapshot().tracks) {
                require(t.ambiguous,"overlap must report assignment ambiguity in both stages");
                require(!focus.select(t.id,overlap.snapshot()),"ambiguous selection rejected");
            }
        }
    }
    {
        auto plain=cfg;
        auto maneuver=cfg;
        maneuver.maneuverRadius=96;
        maneuver.maneuverAccel=900;
        auto run=[&](const Config& config) {
            Tracker tracker(config);
            for(int i=0;i<8;++i) require(tracker.update(i,step*i,{person(40+4.f*i)}),"approach");
            require(tracker.update(8,step*8,{person(0)}),"reversal");
            return tracker.snapshot();
        };
        const auto lost=run(plain);
        const auto kept=run(maneuver);
        bool confirmed_original=false, birthed=false;
        for(const auto& t:lost.tracks) {
            if(t.id==1 && t.state==State::Confirmed) confirmed_original=true;
            if(t.id!=1) birthed=true;
        }
        require(!confirmed_original && birthed,"mahalanobis gate does not absorb a reversal");
        require(kept.tracks.size()==1 && kept.tracks[0].id==1 && kept.tracks[0].state==State::Confirmed,
                "a unique detection inside the maneuver radius keeps the track id");
        require(kept.tracks[0].x[2]<0,"maneuver velocity follows the reversal");
    }
    std::cout<<"Synthetic sequences passed: 1/2/5/10 people, crossing, head-only, occlusion, selection, stale results, bytetrack\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n"; return 1;}

