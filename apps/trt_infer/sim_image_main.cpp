#include "image_detection.hpp"
#include "vision/sim/Simulator.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) try {
  if(argc!=6) throw std::runtime_error("usage: vision_sim_image engine sprite background tracker new-output");
  const std::filesystem::path output(argv[5]);
  if(std::filesystem::exists(output)) throw std::runtime_error("Output must be new");
  Detector detector(argv[1]);
  sim::ImageObservation images;
  images.sprite=cv::imread(argv[2]);images.background=cv::imread(argv[3]);
  if(images.sprite.empty() || images.background.empty()) throw std::runtime_error("Missing scene images");
  std::filesystem::create_directories(output);
  std::ofstream detections(output/"detections.jsonl");
  detections.exceptions(std::ios::badbit|std::ios::failbit);
  cv::VideoWriter video((output/"scene.avi").string(),cv::VideoWriter::fourcc('M','J','P','G'),25,{320,320});
  if(!video.isOpened()) throw std::runtime_error("Cannot open scene video");
  images.detect=[&](const cv::Mat& frame,int id,std::int64_t time) {
    auto boxes=detect(detector,frame,.25f,.45f);
    detections<<"{\"frame\":"<<id<<",\"ts_ns\":"<<time<<",\"detections\":"<<boxesJson(boxes)<<"}\n";
    video.write(frame);
    std::vector<mot::Detection> result;
    for(const auto& b:boxes) result.push_back({b.cls,b.score,b.box});
    return result;
  };
  sim::ScenarioConfig config;
  config.width=config.height=320;config.start_x=110;config.start_y=160;
  config.target_w=images.sprite.cols;config.target_h=images.sprite.rows;
  config.vx=0;config.frames=100;config.noise_px=0;
  config.acquire_single_confirmed=true;config.incremental_control=true;
  config.view_feedback=true;config.encoder_compensation=true;config.pid_kp=2;
  auto result=sim::Simulator(config,mot::Config::load(argv[4]),images).run();
  std::ofstream frames(output/"feedback.jsonl");frames.exceptions(std::ios::badbit|std::ios::failbit);
  for(const auto& f:result.frames)
    frames<<"{\"frame\":"<<f.frame<<",\"camera_x\":"<<f.camera_x<<",\"camera_y\":"<<f.camera_y
      <<",\"truth_x\":"<<f.truth_x<<",\"truth_y\":"<<f.truth_y<<",\"error_px\":"<<f.error_px
      <<",\"track_id\":"<<f.track_id<<"}\n";
  std::cout<<sim::name(result.result)<<" MAE="<<result.metrics.mae_px<<" coverage="<<result.metrics.track_coverage<<"\n";
  return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
