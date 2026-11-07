#include "image_detection.hpp"
#include <opencv2/imgcodecs.hpp>
#include <iostream>

int main(int argc,char** argv) try {
    if(argc!=3) throw std::runtime_error("usage: vision_detect engine image");
    auto image=cv::imread(argv[2]);
    if(image.empty()) throw std::runtime_error("Cannot decode image");
    Detector detector(argv[1]);
    std::cout<<boxesJson(detect(detector,image,.25f,.45f))<<"\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<"\n";
    return 1;
}
