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
  if(argc!=6 && (argc<10 || argc>17)) throw std::runtime_error("usage: vision_sim_image engine sprite background tracker new-output [target-x target-y target-width target-height [selected-track-id [units-per-pixel [subpixel-render-0-or-1 [observation-delay-ms [measured-service-0-or-1 [observation-jitter-ms [legacy|human_led|human_led_off]]]]]]]]");
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
  double last_service_ms=0;
  if(argc>=15) {const int flag=std::stoi(argv[14]);if(flag!=0 && flag!=1) throw std::invalid_argument("Invalid service flag");if(flag) images.service_time_ms=[&]{return last_service_ms;};}
  images.detect=[&](const cv::Mat& frame,int id,std::int64_t time) {
    const auto begin=std::chrono::steady_clock::now();
    auto boxes=detect(detector,frame,.25f,.45f);
    const double detect_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    last_service_ms=detect_ms;
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
  if(argc>=14) config.observation_delay_ms=std::stod(argv[13]);
  if(argc>=16) config.observation_jitter_ms=std::stod(argv[15]);
  if(argc>=17) {
    const std::string assist_arg=argv[16];
    if(assist_arg!="legacy" && assist_arg!="human_led" && assist_arg!="human_led_off")
      throw std::invalid_argument("assist mode must be legacy, human_led, or human_led_off");
    if(assist_arg!="legacy") {
      // Experimental region/deadzone/step/resume stay on ScenarioConfig defaults.
      // human_led places the annotated target 20 px right of center so the trim
      // has an inside-region error to remove. It does not chase a far target.
      config.assist_mode=sim::AssistMode::HumanLed;
      config.assist_enabled=assist_arg=="human_led";
      config.assist_center_x=config.width*0.5;
      config.assist_center_y=config.height*0.5;
      config.actuator_delay_frames=0;
      config.start_x=static_cast<float>(config.assist_center_x+20);
      config.start_y=static_cast<float>(config.assist_center_y);
      config.vx=0;
      config.vy=0;
    }
  }
  config.view_feedback=true;config.encoder_compensation=true;config.pid_kp=2;
  auto result=sim::Simulator(config,mot::Config::load(argv[4]),images).run();
  std::ofstream frames(output/"feedback.jsonl");frames.exceptions(std::ios::badbit|std::ios::failbit);
  std::map<std::string,int> gates;
  for(const auto& f:result.frames) {
    ++gates[f.control_gate];
    frames<<"{\"observation_frame\":"<<f.observation_frame<<",\"observation_ts_ns\":"<<f.observation_ts_ns<<",\"frame\":"<<f.frame<<",\"camera_x\":"<<f.camera_x<<",\"camera_y\":"<<f.camera_y
      <<",\"truth_x\":"<<f.truth_x<<",\"truth_y\":"<<f.truth_y<<",\"error_px\":"<<f.error_px
      <<",\"track_id\":"<<f.track_id<<",\"track_state\":\""<<f.track_state
      <<"\",\"control_gate\":\""<<f.control_gate<<"\",\"covariance_trace\":"<<f.covariance_trace
      <<",\"est_x\":"<<f.est_x<<",\"est_y\":"<<f.est_y
      <<",\"assist_state\":\""<<f.assist_state<<"\",\"assist_reason\":\""<<f.assist_reason
      <<"\",\"assist_dx_px\":"<<f.assist_dx_px<<",\"assist_dy_px\":"<<f.assist_dy_px<<"}\n";
  }
  std::ofstream commands(output/"commands.jsonl");commands.exceptions(std::ios::badbit|std::ios::failbit);
  commands.precision(17);
  for(const auto& e:result.commands)
    commands<<"{\"kind\":\""<<e.kind<<"\",\"id\":"<<e.id<<",\"frame_id\":"<<e.frame_id
      <<",\"track_id\":"<<e.track_id<<",\"timestamp\":"<<e.timestamp<<",\"generated\":"<<e.generated
      <<",\"arrival\":"<<e.arrival<<",\"expires\":"<<e.expires<<",\"dx\":"<<e.dx<<",\"dy\":"<<e.dy<<"}\n";
  std::ofstream observations(output/"observations.jsonl");observations.exceptions(std::ios::badbit|std::ios::failbit);
  for(const auto& e:result.observations) observations<<"{\"frame\":"<<e.frame<<",\"captured\":"<<e.captured
    <<",\"ready\":"<<e.ready<<",\"released\":"<<e.released<<",\"service_ms\":"<<e.service_ms
    <<",\"status\":\""<<e.status<<"\"}\n";
  std::ofstream summary(output/"summary.json");summary.exceptions(std::ios::badbit|std::ios::failbit);
  summary<<"{\"result\":\""<<sim::name(result.result)<<"\",\"terminal_frame\":"<<result.terminal_frame
    <<",\"result_semantics\":\"first_terminal_event_latched\""
    <<",\"success_maintained\":"<<(result.metrics.success_maintained?"true":"false")
    <<",\"end_stable\":"<<(result.metrics.end_stable?"true":"false")
    <<",\"post_success_frames\":"<<result.metrics.post_success_frames
    <<",\"post_success_violations\":"<<result.metrics.post_success_violations
    <<",\"first_post_success_violation\":"<<result.metrics.first_post_success_violation
    <<",\"end_stable_frames\":"<<result.metrics.end_stable_frames
    <<",\"mae_px\":"<<result.metrics.mae_px<<",\"track_coverage\":"<<result.metrics.track_coverage
    <<",\"selected_track_id\":"<<config.selected_track_id<<",\"acquire_single_confirmed\":"<<(config.acquire_single_confirmed?"true":"false")
    <<",\"observation_delay_ms\":"<<config.observation_delay_ms
    <<",\"measured_service\":"<<(images.service_time_ms?"true":"false")<<",\"observation_jitter_ms\":"<<config.observation_jitter_ms
    <<",\"units_per_px\":"<<config.actuator_units_per_px<<",\"subpixel_render\":"<<(config.subpixel_render?"true":"false")
    <<",\"clock\":\"fixed_simulation_step_not_wall_time\",\"encoder_feedback\":true"
    <<",\"assist\":{\"mode\":\""<<result.assist.mode<<"\",\"protocol\":\""<<result.assist.protocol
    <<"\",\"parameters_experimental\":"<<(result.assist.parameters_experimental?"true":"false")
    <<",\"enabled\":"<<(result.assist.enabled?"true":"false")
    <<",\"center_x\":"<<result.assist.center_x<<",\"center_y\":"<<result.assist.center_y
    <<",\"enter_px\":"<<result.assist.enter_px<<",\"exit_px\":"<<result.assist.exit_px
    <<",\"deadzone_px\":"<<result.assist.deadzone_px<<",\"max_step_px\":"<<result.assist.max_step_px
    <<",\"resume_wait_s\":"<<result.assist.resume_wait_s
    <<",\"note\":\"region_deadzone_step_resume_are_experimental\"},\"gates\":{";
  bool first=true;
  for(const auto& g:gates) {summary<<(first?"":",")<<"\""<<g.first<<"\":"<<g.second;first=false;}
  summary<<"}}\n";
  std::cout<<sim::name(result.result)<<" MAE="<<result.metrics.mae_px<<" coverage="<<result.metrics.track_coverage<<"\n";
  return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
