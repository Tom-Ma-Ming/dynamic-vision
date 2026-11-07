#include "tracker.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mot {
Config Config::load(const std::string& path) {
    cv::FileStorage f(path,cv::FileStorage::READ);
    if (!f.isOpened()) throw std::runtime_error("Cannot open tracker config: "+path);
    auto get=[&](const char* key) {
        if(f[key].empty()) throw std::runtime_error(std::string("Missing tracker setting: ")+key);
        return static_cast<double>(f[key]);
    };
    Config c{int(get("min_hits")),get("max_lost_ms"),get("birth_score"),get("measurement_sigma"),
             get("head_sigma"),get("acceleration_sigma"),get("initial_speed_sigma"),
             get("mahalanobis_gate"),get("iou_weight"),get("ambiguity_margin")};
    c.byteTrack = !f["byte_track"].empty() && static_cast<int>(f["byte_track"]) != 0;
    c.highScore = f["high_score"].empty() ? c.birthScore : get("high_score");
    c.lowScore = f["low_score"].empty() ? 0.1 : get("low_score");
    if(!(c.minHits>=1 && c.maxLostMs>0 && c.birthScore>=0 && c.birthScore<=1 && c.sigma>0 &&
         c.headSigma>0 && c.accel>0 && c.initialSpeed>0 && c.gate>0 &&
         c.iouWeight>=0 && c.iouWeight<=1 && c.ambiguity>=0 &&
         c.highScore>=c.lowScore && c.lowScore>=0 && c.highScore<=1))
        throw std::runtime_error("Invalid tracker configuration");
    return c;
}
const char* name(State s) {
    if(s==State::Confirmed) return "Confirmed";
    return s==State::Lost ? "Lost" : "Tentative";
}
cv::Rect2f Track::box() const {
    return {float(x[0])-size.width/2,float(x[1])-size.height/2,size.width,size.height};
}
static cv::Point2f center(cv::Rect2f b) { return (b.tl()+b.br())*.5f; }
struct Observation { Detection d; std::optional<cv::Point2f> head; bool ambiguous=false; };
static std::vector<Observation> group(const std::vector<Detection>& detections) {
    std::vector<Observation> obs;
    for(const auto& d:detections) if(d.cls==1) obs.push_back({d});
    auto bodies=obs.size();
    for(const auto& d:detections) {
        if(d.cls!=0) continue;
        auto h=center(d.box);
        size_t best=bodies;
        double distance=std::numeric_limits<double>::infinity();
        int count=0;
        for(size_t j=0;j<bodies;++j) {
            auto b=obs[j].d.box;
            if(!b.contains(h)) continue;
            ++count;
            auto expected=cv::Point2f(center(b).x,b.y+d.box.height/2);
            double delta=cv::norm(h-expected);
            if(delta<distance) { best=j; distance=delta; }
        }
        if(best==bodies) obs.push_back({d,h});
        else {
            obs[best].ambiguous |= count>1 || obs[best].head.has_value();
            if(!obs[best].head) obs[best].head=h;
        }
    }
    return obs;
}
static void predict(Track& t,double dt,double accel) {
    auto f=cv::Matx44d::eye(); f(0,2)=dt; f(1,3)=dt;
    cv::Matx<double,4,2> g(dt*dt/2,0,0,dt*dt/2,dt,0,0,dt);
    t.x=f*t.x;
    t.covariance=f*t.covariance*f.t()+accel*accel*(g*g.t());
}
Snapshot Tracker::predictAt(Time ts) const {
    if(ts<current.timestamp) throw std::invalid_argument("Prediction before snapshot");
    auto s=current;
    s.timestamp=ts;
    for(auto& t:s.tracks) predict(t,(ts-current.timestamp)*1e-9,cfg.accel);
    s.tracks.erase(std::remove_if(s.tracks.begin(),s.tracks.end(),[&](const auto& t){
        return (ts-t.lastSeen)*1e-6>cfg.maxLostMs;
    }),s.tracks.end());
    return s;
}

// Rectangular Hungarian assignment, one private unmatched option per row.
static std::vector<int> assign(const std::vector<std::vector<double>>& cost) {
    const int n=int(cost.size());
    if(!n) return {};
    int m=int(cost[0].size());
    std::vector<double> u(n+1),v(m+1);
    std::vector<int> owner(m+1),prev(m+1);
    for(int i=1;i<=n;++i) {
        owner[0]=i;
        int j=0;
        std::vector<double> slack(m+1,1e15);
        std::vector<bool> used(m+1);
        do {
            used[j]=true;
            int row=owner[j], next=0;
            double step=1e15;
            for(int k=1;k<=m;++k) if(!used[k]) {
                double d=cost[row-1][k-1]-u[row]-v[k];
                if(d<slack[k]) {slack[k]=d; prev[k]=j;}
                if(slack[k]<step) {step=slack[k]; next=k;}
            }
            for(int k=0;k<=m;++k) {
                if(used[k]) {u[owner[k]]+=step; v[k]-=step;}
                else slack[k]-=step;
            }
            j=next;
        } while(owner[j]);
        do {int k=prev[j]; owner[j]=owner[k]; j=k;} while(j);
    }
    std::vector<int> match(n,-1);
    for(int j=1;j<=m;++j) if(owner[j]) match[owner[j]-1]=j-1;
    return match;
}
static const cv::Matx<double,2,4> H(1,0,0,0,0,1,0,0);
struct Candidate {cv::Vec2d residual; cv::Matx22d inv; double variance,d2,cost;};
static Candidate candidate(const Track& t,const Observation& o,const Config& c) {
    bool body=o.d.cls==1;
    auto anchor=center(o.d.box);
    if(t.body && !body) anchor-=t.headOffset;
    if(!t.body && body) anchor=o.head.value_or(cv::Point2f(anchor.x,o.d.box.y+t.size.height/2));
    cv::Vec2d r(anchor.x-t.x[0],anchor.y-t.x[1]);
    double sigma=t.body && body ? c.sigma : c.headSigma;
    double var=sigma*sigma;
    auto inv=(H*t.covariance*H.t()+cv::Matx22d::eye()*var).inv();
    double d2=r.dot(inv*r);
    double common=(t.box() & o.d.box).area();
    double iou=common/(t.box().area()+o.d.box.area()-common);
    return {r,inv,var,d2,c.iouWeight*(1-iou)+(1-c.iouWeight)*d2/c.gate};
}
static bool assignmentAmbiguous(const std::vector<std::vector<double>>& costs,
                                size_t row, size_t col, size_t observations,
                                double margin) {
    double chosen=costs[row][col];
    for(size_t j=0;j<observations;++j)
        if(j!=col && costs[row][j]<=chosen+margin) return true;
    for(size_t i=0;i<costs.size();++i)
        if(i!=row && costs[i][col]<=chosen+margin) return true;
    return false;
}
bool Tracker::update(Time frame,Time ts,const std::vector<Detection>& detections) {
    if(frame<=current.frame || ts<=current.timestamp) return false;
    if(cfg.byteTrack) {
        auto s=predictAt(ts);
        s.removed.clear();
        for(auto& t:current.tracks)
            if((ts-t.lastSeen)*1e-6>cfg.maxLostMs) s.removed.push_back(t.id);
        auto obs=group(detections);
        std::vector<Observation> high,low;
        for(auto& o:obs) {
            if(o.d.score>=cfg.highScore) high.push_back(o);
            else if(o.d.score>=cfg.lowScore) low.push_back(o);
        }
        auto apply=[&](Track& t,const Observation& o,const Candidate& c) {
            auto k=t.covariance*H.t()*c.inv;
            auto a=cv::Matx44d::eye()-k*H;
            t.x+=k*c.residual;
            t.covariance=a*t.covariance*a.t()+c.variance*(k*k.t());
            if(!t.body && o.d.cls==1) {
                auto anchor=o.head.value_or(cv::Point2f(center(o.d.box).x,o.d.box.y+t.size.height/2));
                auto offset=center(o.d.box)-anchor;
                t.x[0]+=offset.x; t.x[1]+=offset.y;
                t.headOffset=-offset;
            }
            if(o.d.cls==1 || !t.body) t.size=o.d.box.size();
            if(o.d.cls==1) {
                t.body=true;
                if(o.head) t.headOffset=*o.head-center(o.d.box);
            }
            t.score=o.d.score; t.cost=c.cost; t.innovation=c.d2;
            t.ambiguous=o.ambiguous;
            t.lastSeen=ts;
            if(++t.hits>=cfg.minHits) t.state=State::Confirmed;
        };
        const size_t n=s.tracks.size(),m=high.size();
        const double unmatched=double(n)+2, forbidden=unmatched*2;
        std::vector<std::vector<double>> costs(n,std::vector<double>(m+n,forbidden));
        for(size_t i=0;i<n;++i) {
            costs[i][m+i]=unmatched;
            for(size_t j=0;j<m;++j) {
                auto c=candidate(s.tracks[i],high[j],cfg);
                if(c.d2<=cfg.gate) costs[i][j]=c.cost;
            }
        }
        auto matches=assign(costs);
        std::vector<bool> used_high(m);
        std::vector<Track> live, unmatched_tracks;
        for(size_t i=0;i<n;++i) {
            auto t=s.tracks[i];
            size_t j=matches[i];
            if(j>=m) {
                if(t.state==State::Tentative) s.removed.push_back(t.id);
                else unmatched_tracks.push_back(t);
            } else {
                used_high[j]=true;
                auto c=candidate(t,high[j],cfg);
                apply(t,high[j],c);
                t.ambiguous |= assignmentAmbiguous(costs,i,j,m,cfg.ambiguity);
                live.push_back(t);
            }
        }
        const size_t n2=unmatched_tracks.size(),m2=low.size();
        if(n2 && m2) {
            const double unmatched2=double(n2)+2, forbidden2=unmatched2*2;
            std::vector<std::vector<double>> costs2(n2,std::vector<double>(m2+n2,forbidden2));
            for(size_t i=0;i<n2;++i) {
                costs2[i][m2+i]=unmatched2;
                for(size_t j=0;j<m2;++j) {
                    auto c=candidate(unmatched_tracks[i],low[j],cfg);
                    if(c.d2<=cfg.gate) costs2[i][j]=c.cost;
                }
            }
            auto matches2=assign(costs2);
            for(size_t i=0;i<n2;++i) {
                auto t=unmatched_tracks[i];
                size_t j=matches2[i];
                if(j>=m2) {
                    t.state=State::Lost;
                    t.cost=t.innovation=-1;
                } else {
                    auto c=candidate(t,low[j],cfg);
                    apply(t,low[j],c);
                    t.ambiguous |= assignmentAmbiguous(costs2,i,j,m2,cfg.ambiguity);
                }
                live.push_back(t);
            }
        } else {
            for(auto& t:unmatched_tracks) {
                t.state=State::Lost;
                t.cost=t.innovation=-1;
                live.push_back(t);
            }
        }
        for(size_t j=0;j<m;++j) if(!used_high[j] && high[j].d.score>=cfg.birthScore) {
            const auto& o=high[j];
            Track t{};
            t.id=nextId++; t.size=o.d.box.size(); t.lastSeen=ts; t.score=o.d.score;
            auto p=center(o.d.box); t.x={p.x,p.y,0,0}; t.body=o.d.cls==1;
            t.headOffset=o.head.value_or(cv::Point2f(p.x,o.d.box.y))-p;
            t.ambiguous=o.ambiguous;
            t.covariance=cv::Matx44d::eye();
            for(int a=0;a<4;++a) t.covariance(a,a)=std::pow(a<2?cfg.headSigma:cfg.initialSpeed,2);
            if(cfg.minHits==1) t.state=State::Confirmed;
            live.push_back(t);
        }
        s.tracks=std::move(live); s.frame=frame; current=std::move(s);
        return true;
    }
    auto s=predictAt(ts);
    s.removed.clear();
    for(auto& t:current.tracks)
        if((ts-t.lastSeen)*1e-6>cfg.maxLostMs) s.removed.push_back(t.id);
    auto obs=group(detections);
    size_t n=s.tracks.size(),m=obs.size();
    const double unmatched=double(n)+2, forbidden=unmatched*2;
    std::vector<std::vector<double>> costs(n,std::vector<double>(m+n,forbidden));
    for(size_t i=0;i<n;++i) {
        costs[i][m+i]=unmatched;
        for(size_t j=0;j<m;++j) {
            auto c=candidate(s.tracks[i],obs[j],cfg);
            if(c.d2<=cfg.gate) costs[i][j]=c.cost;
        }
    }
    auto matches=assign(costs);
    std::vector<bool> used(m);
    std::vector<Track> live;
    for(size_t i=0;i<n;++i) {
        auto t=s.tracks[i];
        size_t j=matches[i];
        if(j>=m) {
            if(t.state==State::Tentative) { s.removed.push_back(t.id); continue; }
            t.state=State::Lost;
            t.cost=t.innovation=-1;
        } else {
            used[j]=true;
            auto& o=obs[j];
            auto c=candidate(t,o,cfg);
            auto k=t.covariance*H.t()*c.inv;
            auto a=cv::Matx44d::eye()-k*H;
            t.x+=k*c.residual;
            t.covariance=a*t.covariance*a.t()+c.variance*(k*k.t());
            if(!t.body && o.d.cls==1) {
                auto anchor=o.head.value_or(cv::Point2f(center(o.d.box).x,o.d.box.y+t.size.height/2));
                auto offset=center(o.d.box)-anchor;
                t.x[0]+=offset.x; t.x[1]+=offset.y;
                t.headOffset=-offset;
            }
            if(o.d.cls==1 || !t.body) t.size=o.d.box.size();
            if(o.d.cls==1) {
                t.body=true;
                if(o.head) t.headOffset=*o.head-center(o.d.box);
            }
            t.score=o.d.score; t.cost=c.cost; t.innovation=c.d2;
            t.ambiguous=o.ambiguous;
            t.ambiguous |= assignmentAmbiguous(costs,i,j,m,cfg.ambiguity);
            t.lastSeen=ts;
            if(++t.hits>=cfg.minHits) t.state=State::Confirmed;
        }
        live.push_back(t);
    }
    for(size_t j=0;j<m;++j) if(!used[j] && obs[j].d.score>=cfg.birthScore) {
        const auto& o=obs[j];
        Track t{};
        t.id=nextId++; t.size=o.d.box.size(); t.lastSeen=ts; t.score=o.d.score;
        auto p=center(o.d.box); t.x={p.x,p.y,0,0}; t.body=o.d.cls==1;
        t.headOffset=o.head.value_or(cv::Point2f(p.x,o.d.box.y))-p;
        t.ambiguous=o.ambiguous;
        t.covariance=cv::Matx44d::eye();
        for(int a=0;a<4;++a) t.covariance(a,a)=std::pow(a<2?cfg.headSigma:cfg.initialSpeed,2);
        if(cfg.minHits==1) t.state=State::Confirmed;
        live.push_back(t);
    }
    s.tracks=std::move(live); s.frame=frame; current=std::move(s);
    return true;
}
bool Selection::select(Id id,const Snapshot& s) {
    for(auto& t:s.tracks) if(t.id==id && t.state==State::Confirmed && !t.ambiguous) {
        selected=id; return true;
    }
    return false;
}
const char* Selection::state(const Snapshot& s) const {
    if(!selected) return "None";
    for(auto& t:s.tracks) if(t.id==*selected) {
        if(t.ambiguous) return "Unavailable";
        return t.state==State::Lost?"TemporarilyLost":"Selected";
    }
    return "Unavailable";
}
}
