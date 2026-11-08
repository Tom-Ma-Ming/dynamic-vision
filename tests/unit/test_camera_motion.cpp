#include "tracker.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
cv::Mat texture(unsigned seed=77) {
    cv::Mat image(320,320,CV_8U);
    cv::RNG rng(seed);rng.fill(image,cv::RNG::UNIFORM,0,256);
    cv::GaussianBlur(image,image,{3,3},.7);
    return image;
}
cv::Mat shifted(const cv::Mat& image,double x,double y) {
    cv::Mat output;
    cv::warpAffine(image,output,cv::Matx23d(1,0,x,0,1,y),image.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT);
    return output;
}
int main(int argc,char** argv) try {
    require(argc==2,"usage: camera_motion_test tracker-camera.yml");
    auto cfg=mot::Config::load(argv[1]);
    const mot::Time step=40000000;
    auto image=texture();
    mot::CameraMotionEstimator estimator(cfg.cameraMotion);
    require(!estimator.update(image,0,{}).accepted,"first frame must initialize");
    auto movement=estimator.update(shifted(image,12,-7),step,{});
    require(movement.accepted,"known translation rejected");
    require(cv::norm(movement.shift-cv::Point2d(12,-7))<.5,"translation direction/accuracy");
    require(movement.variance>=.25,"translation uncertainty floor");
    auto stale=estimator.update(image,step-1,{});
    require(!stale.accepted && stale.reason=="stale_timestamp","stale estimator input");
    auto stationary=estimator.update(shifted(image,12,-7),step*2,{});
    require(stationary.accepted && cv::norm(stationary.shift)<.1,"stale frame mutated reference");
    auto gap=estimator.update(image,step*20,{});
    require(!gap.accepted && gap.reason=="time_gap","gap should reset");
    auto size=estimator.update(cv::Mat::zeros(160,160,CV_8U),step*21,{});
    require(!size.accepted && size.reason=="resolution_change","resize should reset");
    auto flat=estimator.update(cv::Mat::zeros(160,160,CV_8U),step*22,{});
    require(!flat.accepted && flat.reason=="few_features","flat texture should reject");

    mot::CameraMotionEstimator foreground(cfg.cameraMotion);
    auto first=image.clone(),second=shifted(image,8,5);
    cv::rectangle(first,{60,80,70,100},cv::Scalar(255),cv::FILLED);
    cv::rectangle(second,{140,90,70,100},cv::Scalar(255),cv::FILLED);
    foreground.update(first,0,{{60,80,70,100}});
    auto masked=foreground.update(second,step,{{140,90,70,100}});
    std::cout<<"Masked translation: "<<masked.reason<<" shift="<<masked.shift
             <<" inliers="<<masked.inliers<<"/"<<masked.features<<" coverage="<<masked.coverage<<"\n";
    require(masked.accepted && cv::norm(masked.shift-cv::Point2d(8,5))<.5,"moving foreground corrupted background");
    auto cut=foreground.update(texture(934),step*2,{});
    require(!cut.accepted,"unrelated scene should reject");
    // A textured patch alone cannot establish global background motion.
    mot::CameraMotionEstimator localized(cfg.cameraMotion);
    cv::Mat patch=cv::Mat::zeros(320,320,CV_8U);
    image(cv::Rect(70,70,60,60)).copyTo(patch(cv::Rect(70,70,60,60)));
    localized.update(patch,0,{});
    require(!localized.update(shifted(patch,5,3),step,{}).accepted,"localized features accepted as global motion");

    for(bool imm:{false,true}) for(bool byte:{false,true}) {
        cfg.imm.enabled=imm; cfg.byteTrack=byte;
        mot::Tracker tracker(cfg);
        for(int frame=0;frame<30;++frame) tracker.update(frame,frame*step,{{1,.9f,{90,90,20,40}}});
        require(tracker.snapshot().tracks.size()==1,"stationary track initialization");
        const auto id=tracker.snapshot().tracks[0].id;
        mot::CameraMotionEstimate camera;
        camera.from=29*step;camera.to=30*step;camera.accepted=true;camera.shift={30,-10};camera.variance=.25;
        require(tracker.update(30,30*step,{{1,.9f,{120,80,20,40}}},&camera),"translated update rejected");
        const auto& track=tracker.snapshot().tracks.at(0);
        require(tracker.snapshot().tracks.size()==1 && track.id==id && track.state==mot::State::Confirmed,"camera shift fragmented ID");
        require(std::hypot(track.x[0]-130,track.x[1]-100)<.01,"wrong translated anchor");
        require(std::hypot(track.x[2],track.x[3])<.01,"camera displacement became target velocity");
        require(track.lastSeen==30*step,"observation timestamp");
        auto original=tracker.snapshot();
        tracker.predictAt(31*step);
        require(cv::norm(tracker.snapshot().tracks[0].x-original.tracks[0].x)==0,"prediction mutated compensated state");
        bool rejected=false;
        try {tracker.update(31,31*step,{},&camera);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && tracker.snapshot().frame==30,"mismatched motion mutated tracker");
        camera.from=30*step;camera.to=31*step;camera.shift.x=std::numeric_limits<double>::quiet_NaN();
        rejected=false;
        try {tracker.update(31,31*step,{},&camera);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && tracker.snapshot().frame==30,"nonfinite motion accepted");
        camera.shift={7,3};
        tracker.update(31,31*step,{},&camera);
        const auto& lost=tracker.snapshot().tracks.at(0);
        require(lost.state==mot::State::Lost && lost.lastSeen==30*step,"camera-only update must not mark observed");
        require(std::hypot(lost.x[0]-137,lost.x[1]-103)<.01,"lost track translation");
        require(lost.covariance(0,0)>original.tracks[0].covariance(0,0),"translation did not retain uncertainty");
        if(imm) require(cv::norm(lost.imm->fused().mean-lost.x)<1e-9,"IMM models not translated consistently");
        mot::Tracker headOnly(cfg);
        headOnly.update(0,0,{{0,.9f,{96,90,8,8}}});
        headOnly.update(1,step,{{0,.9f,{96,90,8,8}}});
        camera.from=step;camera.to=2*step;camera.shift={20,5};
        headOnly.update(2,2*step,{{0,.9f,{116,95,8,8}},{1,.9f,{110,95,20,40}}},&camera);
        const auto& transitioned=headOnly.snapshot().tracks.at(0);
        require(headOnly.snapshot().tracks.size()==1 && transitioned.id==1 && transitioned.body,"camera head/body transition");
        require(std::hypot(transitioned.x[0]-120,transitioned.x[1]-115)<.01,"head/body anchor shift");
        if(imm) require(cv::norm(transitioned.imm->fused().mean-transitioned.x)<1e-9,"IMM head/body models disagree");
    }
    cfg.imm.enabled=false;cfg.byteTrack=false;cfg.cameraMotion.enabled=false;
    mot::Tracker disabled(cfg);
    for(int frame=0;frame<30;++frame) disabled.update(frame,frame*step,{{1,.9f,{90,90,20,40}}});
    mot::CameraMotionEstimate ignored;
    ignored.accepted=true;ignored.from=29*step;ignored.to=30*step;ignored.shift={30,-10};ignored.variance=.25;
    disabled.update(30,30*step,{{1,.9f,{120,80,20,40}}},&ignored);
    for(const auto& track:disabled.snapshot().tracks)
        require(track.id!=1 || track.lastSeen==29*step,"disabled compensation changed association");
    auto invalid=cfg.cameraMotion;invalid.minInlierRatio=1.1;
    bool rejected=false;
    try {invalid.validate();} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"invalid config accepted");
    std::cout<<"Camera motion: translation, masks, reliability, clocks and CV/IMM/ByteTrack passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
