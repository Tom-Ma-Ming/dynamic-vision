#include "camera_motion.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mot {
void CameraMotionConfig::validate() const {
    for(double v:{maxFbError,residualThreshold,minInlierRatio,minCoverage,maxShiftFraction,maxGapMs})
        if(!std::isfinite(v) || v<=0) throw std::invalid_argument("Invalid camera motion parameter");
    for(double v:{foregroundMargin,borderFraction,centerRadius})
        if(!std::isfinite(v) || v<0) throw std::invalid_argument("Invalid camera motion mask parameter");
    if(maxCorners<minInliers || maxCorners>5000 || minInliers<4 || minGridCells<1 || minGridCells>9 ||
       minInlierRatio>1 || minCoverage>1 || maxShiftFraction>1 || borderFraction>=.45 ||
       foregroundMargin>4096 || centerRadius>4096 || maxGapMs>60000)
        throw std::invalid_argument("Camera motion parameter out of range");
}
namespace {
double median(std::vector<double> values) {
    auto middle=values.begin()+values.size()/2;
    std::nth_element(values.begin(),middle,values.end());
    return *middle;
}
bool visible(const cv::Mat& mask,cv::Point2f p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.y>=0 &&
           p.x<mask.cols && p.y<mask.rows && mask.at<unsigned char>(int(p.y),int(p.x))!=0;
}
}
CameraMotionEstimate CameraMotionEstimator::update(const cv::Mat& image,std::int64_t timestamp,
                                                  const std::vector<cv::Rect2f>& foreground) {
    if(image.empty() || image.depth()!=CV_8U || (image.channels()!=1 && image.channels()!=3))
        throw std::invalid_argument("Camera motion expects an 8-bit gray/BGR frame");
    CameraMotionEstimate result;
    result.from=previousTimestamp; result.to=timestamp;
    if(timestamp<0 || (!previousGray.empty() && timestamp<=previousTimestamp)) {
        result.reason="stale_timestamp"; return result;
    }
    cv::Mat gray;
    if(image.channels()==3) cv::cvtColor(image,gray,cv::COLOR_BGR2GRAY);
    else gray=image.clone();
    cv::Mat mask(gray.size(),CV_8U,cv::Scalar(0));
    int bx=int(gray.cols*cfg.borderFraction),by=int(gray.rows*cfg.borderFraction);
    cv::rectangle(mask,cv::Rect(bx,by,gray.cols-2*bx,gray.rows-2*by),cv::Scalar(255),cv::FILLED);
    if(cfg.centerRadius>0) cv::circle(mask,{gray.cols/2,gray.rows/2},int(cfg.centerRadius),cv::Scalar(0),cv::FILLED);
    for(auto box:foreground) {
        if(!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) || !std::isfinite(box.height))
            throw std::invalid_argument("Nonfinite foreground box");
        auto expanded=cv::Rect2f(float(box.x-cfg.foregroundMargin),float(box.y-cfg.foregroundMargin),
                                float(box.width+2*cfg.foregroundMargin),float(box.height+2*cfg.foregroundMargin));
        expanded &= cv::Rect2f(0,0,float(gray.cols),float(gray.rows));
        if(expanded.area()>0) cv::rectangle(mask,cv::Rect(expanded),cv::Scalar(0),cv::FILLED);
    }
    auto finish=[&](const char* reason) {
        result.reason=reason;
        previousGray=gray; previousMask=mask; previousTimestamp=timestamp;
        return result;
    };
    if(previousGray.empty()) return finish("initial");
    if(previousGray.size()!=gray.size()) return finish("resolution_change");
    if((timestamp-previousTimestamp)*1e-6>cfg.maxGapMs) return finish("time_gap");
    std::vector<cv::Point2f> points,next,back;
    cv::goodFeaturesToTrack(previousGray,points,cfg.maxCorners,.01,8,previousMask);
    result.features=int(points.size());
    if(result.features<cfg.minInliers) return finish("few_features");
    std::vector<unsigned char> forwardStatus,backStatus;
    std::vector<float> errors;
    const cv::TermCriteria criteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01);
    // Seed translation globally, then verify/refine it using local background tracks.
    // A shallow pyramid limits foreground contamination of coarse LK windows.
    cv::Mat beforeFloat,afterFloat,window;
    previousGray.convertTo(beforeFloat,CV_32F); gray.convertTo(afterFloat,CV_32F);
    beforeFloat.setTo(cv::mean(previousGray,previousMask)[0],previousMask==0);
    afterFloat.setTo(cv::mean(gray,mask)[0],mask==0);
    cv::createHanningWindow(window,gray.size(),CV_32F);
    double phaseResponse=0;
    auto seed=cv::phaseCorrelate(beforeFloat,afterFloat,window,&phaseResponse);
    if(!std::isfinite(seed.x) || !std::isfinite(seed.y) || !std::isfinite(phaseResponse) ||
       phaseResponse<.1 || cv::norm(seed)>cfg.maxShiftFraction*std::min(gray.cols,gray.rows)) seed={0,0};
    for(auto point:points) next.push_back(point+cv::Point2f(float(seed.x),float(seed.y)));
    cv::calcOpticalFlowPyrLK(previousGray,gray,points,next,forwardStatus,errors,{21,21},1,criteria,cv::OPTFLOW_USE_INITIAL_FLOW);
    // Avoid passing failed/nonfinite forward points into the reverse flow call.
    std::vector<cv::Point2f> oldValid,newValid;
    for(size_t i=0;i<points.size();++i) if(forwardStatus[i] && visible(mask,next[i])) {
        oldValid.push_back(points[i]); newValid.push_back(next[i]);
    }
    if(int(oldValid.size())<cfg.minInliers) return finish("few_forward_tracks");
    for(auto point:newValid) back.push_back(point-cv::Point2f(float(seed.x),float(seed.y)));
    cv::calcOpticalFlowPyrLK(gray,previousGray,newValid,back,backStatus,errors,{21,21},1,criteria,cv::OPTFLOW_USE_INITIAL_FLOW);
    std::vector<cv::Point2f> origins;
    std::vector<double> dx,dy;
    for(size_t i=0;i<oldValid.size();++i) if(backStatus[i] && visible(previousMask,back[i]) &&
            cv::norm(back[i]-oldValid[i])<=cfg.maxFbError) {
        origins.push_back(oldValid[i]);
        dx.push_back(newValid[i].x-oldValid[i].x); dy.push_back(newValid[i].y-oldValid[i].y);
    }
    if(int(dx.size())<cfg.minInliers) return finish("few_bidirectional_tracks");
    double mx=median(dx),my=median(dy);
    std::vector<double> ix,iy,residuals;
    std::array<bool,9> cells{};
    double minx=gray.cols,miny=gray.rows,maxx=0,maxy=0;
    for(size_t i=0;i<dx.size();++i) {
        if(std::hypot(dx[i]-mx,dy[i]-my)>cfg.residualThreshold) continue;
        ix.push_back(dx[i]);iy.push_back(dy[i]);
        const auto p=origins[i];
        minx=std::min(minx,double(p.x));maxx=std::max(maxx,double(p.x));
        miny=std::min(miny,double(p.y));maxy=std::max(maxy,double(p.y));
        cells[std::min(2,int(p.y*3/gray.rows))*3+std::min(2,int(p.x*3/gray.cols))]=true;
    }
    result.inliers=int(ix.size());
    // Count against all detected background features, not only surviving flow tracks.
    result.inlierRatio=double(result.inliers)/result.features;
    if(result.inliers<cfg.minInliers || result.inlierRatio<cfg.minInlierRatio) return finish("low_consensus");
    result.coverage=(maxx-minx)*(maxy-miny)/(gray.cols*double(gray.rows));
    result.gridCells=int(std::count(cells.begin(),cells.end(),true));
    if(result.coverage<cfg.minCoverage || result.gridCells<cfg.minGridCells) return finish("poor_spread");
    mx=median(ix);my=median(iy);
    if(std::hypot(mx,my)>cfg.maxShiftFraction*std::min(gray.cols,gray.rows)) return finish("large_shift");
    for(size_t i=0;i<ix.size();++i) residuals.push_back((ix[i]-mx)*(ix[i]-mx)+(iy[i]-my)*(iy[i]-my));
    result.shift={mx,my}; result.variance=std::max(.25,median(residuals));
    result.accepted=true;
    return finish("accepted");
}
}
