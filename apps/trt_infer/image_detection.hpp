#pragma once
#include "detector.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

struct Transform {
    int width, height, left, top;
    explicit Transform(cv::Size size) {
        double scale = 320.0 / std::max(size.width,size.height);
        width = std::max(1,cvRound(size.width*scale));
        height = std::max(1,cvRound(size.height*scale));
        left = (320-width)/2;
        top = (320-height)/2;
    }
    cv::Rect2f restore(cv::Rect2f box, cv::Size src) const {
        float sx = float(src.width)/width, sy = float(src.height)/height;
        cv::Rect2f restored((box.x-left)*sx,(box.y-top)*sy,box.width*sx,box.height*sy);
        return restored & cv::Rect2f(0,0,float(src.width),float(src.height));
    }
};

struct Detection { int cls; float score; cv::Rect2f box; };

inline std::vector<Detection> detect(Detector& detector, const cv::Mat& frame,
                                     float confidence, float iou) {
    Transform tx(frame.size());
    cv::Mat pixels(320,320,CV_8UC3,cv::Scalar::all(114));
    auto region = pixels(cv::Rect(tx.left,tx.top,tx.width,tx.height));
    cv::resize(frame,region,region.size());
    cv::cvtColor(pixels,pixels,cv::COLOR_BGR2RGB);
    cv::Mat floats;
    pixels.convertTo(floats,CV_32F,1./255.);
    float* input=detector.input();
    cv::Mat planes[3];
    for (int c=0;c<3;++c)
        planes[c]=cv::Mat(320,320,CV_32F,input+c*320*320);
    cv::split(floats,planes);
    const float* raw=detector.infer();
    constexpr int n=2100;
    std::vector<Detection> candidates, kept;
    for(int i=0;i<n;++i) {
        int cls=raw[5*n+i]>raw[4*n+i] ? 1 : 0;
        float score=raw[(4+cls)*n+i], w=raw[2*n+i], h=raw[3*n+i];
        if(score>=confidence && w>0 && h>0)
            candidates.push_back({cls,score,{raw[i]-w/2,raw[n+i]-h/2,w,h}});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    for(auto d:candidates) {
        bool suppress=std::any_of(kept.begin(),kept.end(),[&](const Detection& k){
            float common=(k.box & d.box).area();
            return k.cls==d.cls && common > iou*(k.box.area()+d.box.area()-common);
        });
        if(!suppress) kept.push_back(d);
    }
    for(auto& d:kept) d.box=tx.restore(d.box,frame.size());
    kept.erase(std::remove_if(kept.begin(),kept.end(),[](const auto& d){return d.box.empty();}),kept.end());
    return kept;
}

inline std::string boxesJson(const std::vector<Detection>& detections) {
    std::ostringstream out;
    out.precision(9);
    out<<"[";
    bool first=true;
    for(const auto& d:detections) {
        if(!first) out<<",";
        first=false;
        out<<"{\"class_id\":"<<d.cls<<",\"score\":"<<d.score<<",\"xyxy\":["
           <<d.box.x<<","<<d.box.y<<","<<d.box.br().x<<","<<d.box.br().y<<"]}";
    }
    out<<"]";
    return out.str();
}
