#include "tracker.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void checkGaussian(const cv::Vec4d& mean,const cv::Matx44d& covariance) {
    for(double value:mean.val) require(std::isfinite(value),"nonfinite state");
    for(double value:covariance.val) require(std::isfinite(value),"nonfinite covariance");
    require(cv::norm(covariance-covariance.t())<1e-7,"asymmetric covariance");
    cv::Mat eigenvalues;
    require(cv::eigen(cv::Mat(covariance),eigenvalues),"eigendecomposition failed");
    require(eigenvalues.at<double>(3)>=-1e-8,"covariance not PSD");
}
void check(const mot::Track& track) {
    require(track.imm.has_value(),"IMM state missing");
    double sum=0;
    for(double p:track.imm->probabilities) {
        require(std::isfinite(p) && p>=0 && p<=1,"probability out of range");
        sum+=p;
    }
    require(std::abs(sum-1)<1e-10,"probabilities must normalize");
    checkGaussian(track.x,track.covariance);
    for(const auto& model:track.imm->models) checkGaussian(model.mean,model.covariance);
}
int main(int argc,char** argv) try {
    require(argc==2,"usage: imm_test tracker.yml");
    auto cfg=mot::Config::load(argv[1]);
    require(cfg.imm.enabled,"IMM experiment disabled");
    mot::ImmState mixing;
    mixing.initialize({0,0,0,0},cv::Matx44d::eye());
    mixing.models[0].mean[0]=-2;
    mixing.models[2].mean[0]=2;
    auto fused=mixing.fused();
    require(std::abs(fused.mean[0])<1e-10,"mixture mean");
    require(std::abs(fused.covariance(0,0)-(1+8./3))<1e-10,"between-model covariance missing");
    auto before=mixing;
    mixing.predict(0,cfg.accel,cfg.imm);
    require(mixing.probabilities==before.probabilities,"dt0 changes probabilities");
    require(cv::norm(mixing.fused().covariance-before.fused().covariance)==0,"dt0 changes covariance");

    // Identical models and transitions reduce to a single Kalman update.
    mot::ImmState identical;
    auto p=cv::Matx44d::eye()*4;
    identical.initialize({0,0,0,0},p);
    identical.update({10,20},9);
    auto result=identical.fused();
    require(std::abs(result.mean[0]-40./13)<1e-10,"Kalman update mean");
    require(std::abs(result.covariance(0,0)-36./13)<1e-10,"Joseph update covariance");

    for(bool byte : {false,true}) {
        cfg.byteTrack=byte;
        mot::Tracker tracker(cfg);
        double x=50;
        mot::Time ts=0;
        for(int frame=0;frame<1000;++frame) {
            double velocity=frame<150?100:(frame<300?0:(frame<500?-100:50));
            double dt=frame%2==0?.03:.05;
            if(frame!=0) ts+=static_cast<mot::Time>(dt*1e9);
            x+=velocity*dt;
            float noise=float(std::sin(frame*1.7));
            std::vector<mot::Detection> obs;
            if(frame%53!=0 || frame==0)
                obs.push_back({1,frame%7==0?.2f:.9f,{float(x)-8+noise,40,16,32}});
            tracker.update(frame,ts,obs);
            for(const auto& t:tracker.snapshot().tracks) check(t);
            auto original=tracker.snapshot();
            auto future=tracker.predictAt(ts+100000000);
            for(const auto& t:future.tracks) check(t);
            require(tracker.snapshot().timestamp==ts,"forecast mutated timestamp");
            for(size_t i=0;i<original.tracks.size();++i) {
                require(original.tracks[i].imm->probabilities==tracker.snapshot().tracks[i].imm->probabilities,"forecast mutated probabilities");
                require(cv::norm(original.tracks[i].x-tracker.snapshot().tracks[i].x)==0,"forecast mutated state");
            }
        }
    }
    std::cout<<"IMM mixing, Joseph update, irregular time, probability/PSD and snapshot tests passed\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<"\n"; return 1;
}
