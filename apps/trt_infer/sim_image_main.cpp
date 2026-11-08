#include "image_detection.hpp"
#include "vision/sim/Simulator.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <chrono>

int main(int argc,char** argv) try {
  if(argc!=6 && (argc<10 || argc>13)) throw std::runtime_error("usage: vision_sim_image engine sprite background tracker new-output [target-x target-y target-width target-height [selected-track-id [units-per-pixel [subpixel-render-0-or-1]]]]");
  const std::filesystem::path output(argv[5]);
  if(std::filesystem::exists(output)) throw std::runtime_error("Output must be new");
  Detector detector(argv[1]);
  sim::ImageObservation images;
  images.sprite=cv::imread(argv[2]);images.background=cv::imread(argv[3]);
  if(images.sprite.empty() || images.background.empty()) throw std::runtime_error("Missing scene images");
  if(argc>=10) images.sprite_target={std::stof(argv[6]),std::stof(argv[7]),std::stof(argv[8]),std::stof(argv[9])};
  std::filesystem::create_directories(output);
  std::ofstream detections(output/"detections.jsonl");
  detections.exceptions(std::ios::badbit|std::ios::failbit);
  cv::VideoWriter video((output/"scene.avi").string(),cv::VideoWriter::fourcc('M','J','P','G'),25,{320,320});
  if(!video.isOpened()) throw std::runtime_error("Cannot open scene video");
  images.detect=[&](const cv::Mat& frame,int id,std::int64_t time) {
    const auto begin=std::chrono::steady_clock::now();
    auto boxes=detect(detector,frame,.25f,.45f);
    const double detect_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    detections<<"{\"frame\":"<<id<<",\"ts_ns\":"<<time<<",\"detect_ms\":"<<detect_ms<<",\"detections\":"<<boxesJson(boxes)<<"}\n";
    video.write(frame);
    std::vector<mot::Detection> result;
    for(const auto& b:boxes) result.push_back({b.cls,b.score,b.box});
    return result;
  };
  sim::ScenarioConfig config;
  config.width=config.height=320;config.start_x=110;config.start_y=160;
  config.target_w=argc>=10 ? images.sprite_target.width : float(images.sprite.cols);
  config.target_h=argc>=10 ? images.sprite_target.height : float(images.sprite.rows);
  if(argc>=10) {
    config.start_x=images.sprite_target.x+images.sprite_target.width*.5f;
    config.start_y=images.sprite_target.y+images.sprite_target.height*.5f;
  }
  config.vx=0;config.frames=100;config.noise_px=0;
  config.acquire_single_confirmed=true;config.incremental_control=true;
  if(argc>=11) {config.acquire_single_confirmed=false;config.selected_track_id=std::stoi(argv[10]);}
  if(argc>=12) config.actuator_units_per_px=std::stod(argv[11]);
  if(argc>=13) {const int flag=std::stoi(argv[12]);if(flag!=0 && flag!=1) throw std::invalid_argument("Invalid subpixel flag");config.subpixel_render=flag!=0;}
  config.view_feedback=true;config.encoder_compensation=true;config.pid_kp=2;
  auto result=sim::Simulator(config,mot::Config::load(argv[4]),images).run();
  std::ofstream frames(output/"feedback.jsonl");frames.exceptions(std::ios::badbit|std::ios::failbit);
  std::map<std::string,int> gates;
  for(const auto& f:result.frames) {
    ++gates[f.control_gate];
    frames<<"{\"frame\":"<<f.frame<<",\"camera_x\":"<<f.camera_x<<",\"camera_y\":"<<f.camera_y
      <<",\"truth_x\":"<<f.truth_x<<",\"truth_y\":"<<f.truth_y<<",\"error_px\":"<<f.error_px
      <<",\"track_id\":"<<f.track_id<<",\"track_state\":\""<<f.track_state
      <<"\",\"control_gate\":\""<<f.control_gate<<"\",\"covariance_trace\":"<<f.covariance_trace
      <<",\"est_x\":"<<f.est_x<<",\"est_y\":"<<f.est_y<<"}\n";
  }
  std::ofstream commands(output/"commands.jsonl");commands.exceptions(std::ios::badbit|std::ios::failbit);
  commands.precision(17);
  for(const auto& e:result.commands)
    commands<<"{\"kind\":\""<<e.kind<<"\",\"id\":"<<e.id<<",\"frame_id\":"<<e.frame_id
      <<",\"track_id\":"<<e.track_id<<",\"timestamp\":"<<e.timestamp<<",\"generated\":"<<e.generated
      <<",\"arrival\":"<<e.arrival<<",\"expires\":"<<e.expires<<",\"dx\":"<<e.dx<<",\"dy\":"<<e.dy<<"}\n";
  std::ofstream summary(output/"summary.json");summary.exceptions(std::ios::badbit|std::ios::failbit);
  summary<<"{\"result\":\""<<sim::name(result.result)<<"\",\"terminal_frame\":"<<result.terminal_frame
    <<",\"mae_px\":"<<result.metrics.mae_px<<",\"track_coverage\":"<<result.metrics.track_coverage
    <<",\"selected_track_id\":"<<config.selected_track_id<<",\"acquire_single_confirmed\":"<<(config.acquire_single_confirmed?"true":"false")
    <<",\"units_per_px\":"<<config.actuator_units_per_px<<",\"subpixel_render\":"<<(config.subpixel_render?"true":"false")
    <<",\"clock\":\"fixed_simulation_step_not_wall_time\",\"encoder_feedback\":true,\"gates\":{";
  bool first=true;
  for(const auto& g:gates) {summary<<(first?"":",")<<"\""<<g.first<<"\":"<<g.second;first=false;}
  summary<<"}}\n";
  std::cout<<sim::name(result.result)<<" MAE="<<result.metrics.mae_px<<" coverage="<<result.metrics.track_coverage<<"\n";
  return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
