#include "image_detection.hpp"
#include "tracking_output.hpp"
#include <map>
#include <opencv2/videoio.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc,char** argv) try {
    if(argc<4 || argc>6) throw std::runtime_error("usage: vision_replay engine video new-output-directory [tracker.yml] [selection.txt]");
    cv::VideoCapture capture(argv[2]);
    if(!capture.isOpened()) throw std::runtime_error("Cannot open input video");
    double fps=capture.get(cv::CAP_PROP_FPS);
    if(!std::isfinite(fps) || fps<=0) throw std::runtime_error("Missing video FPS");
    auto configPath=argc>=5 ? fs::path(argv[4]) : fs::absolute(argv[0]).parent_path()/"tracker.yml";
    mot::Tracker tracker(mot::Config::load(configPath.string()));
    mot::Selection selection;
    std::map<mot::Time,mot::Id> commands;
    if(argc==6) {
        std::ifstream file(argv[5]);
        if(!file) throw std::runtime_error("Cannot open selection commands");
        mot::Time frameId; mot::Id trackId;
        while(file>>std::ws && file.peek()!=EOF) {
            if(!(file>>frameId>>trackId) || frameId<0 || !commands.emplace(frameId,trackId).second)
                throw std::runtime_error("Expected unique frame_id track_id selection pairs");
        }
    }
    Detector detector(argv[1]);
    cv::Mat frame;
    if(!capture.read(frame)) throw std::runtime_error("Empty or unreadable video");
    fs::path out(argv[3]);
    if(!fs::create_directory(out)) throw std::runtime_error("Use a new output directory");
    std::ofstream log(out/"frames.jsonl");
    log.exceptions(std::ios::badbit|std::ios::failbit);
    cv::VideoWriter writer((out/"overlay.mp4").string(),cv::VideoWriter::fourcc('m','p','4','v'),
                           fps,frame.size());
    if(!writer.isOpened()) throw std::runtime_error("Cannot open overlay writer");
    std::int64_t id=0,last=-1;
    std::vector<double> elapsed, trackingMs;
    do {
        double pts=capture.get(cv::CAP_PROP_POS_MSEC);
        bool timestampOk=std::isfinite(pts) && pts>=0;
        std::int64_t ts=timestampOk ? std::llround(pts*1e6) : -1;
        timestampOk=timestampOk && ts>last;
        if(!timestampOk) ts=last+std::llround(1e9/fps);
        if(!timestampOk && id==0) ts=0;
        last=ts;
        auto start=std::chrono::steady_clock::now();
        auto detections=detect(detector,frame,.25f,.45f);
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        elapsed.push_back(ms);
        auto trackingStart=std::chrono::steady_clock::now();
        std::vector<mot::Detection> observations;
        for(const auto& d:detections) observations.push_back({d.cls,d.score,d.box});
        if(!tracker.update(id,ts,observations)) throw std::runtime_error("Out of order frame");
        trackingMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-trackingStart).count());
        const auto& snapshot=tracker.snapshot();
        auto cmd=commands.find(id);
        if(cmd!=commands.end()) {
            if(!cmd->second) selection.clear();
            else if(!selection.select(cmd->second,snapshot))
                std::cerr<<"Selection unavailable at frame "<<id<<"\n";
        }
        log<<"{\"frame_id\":"<<id++<<",\"source_ts_ns\":"<<ts<<",\"timestamp_source\":\""
           <<(timestampOk?"video_pts":"fps_estimate")<<"\",\"detect_ms\":"<<ms
           <<",\"detections\":"<<boxesJson(detections);
        writeTracking(log,snapshot,selection);
        log<<"}\n";
        for(const auto& d:detections) {
            auto color=d.cls==0 ? cv::Scalar(0,255,255) : cv::Scalar(255,255,0);
            cv::rectangle(frame,d.box,color,1);
            cv::putText(frame,d.cls==0?"head":"person",
                        {cvRound(d.box.x),std::max(10,cvRound(d.box.y)-2)},
                        cv::FONT_HERSHEY_SIMPLEX,.35,color,1,cv::LINE_AA);
        }
        for(const auto& t:snapshot.tracks) {
            if(t.state==mot::State::Tentative) continue;
            auto b=t.box();
            auto color=selection.selected==t.id ? cv::Scalar(80,80,255) : cv::Scalar(80,230,120);
            cv::rectangle(frame,b,color,1);
            std::string label="#"+std::to_string(t.id);
            if(t.state==mot::State::Lost) label+=" lost";
            if(t.ambiguous) label+=" ?";
            cv::putText(frame,label,{cvRound(b.x),std::max(12,cvRound(b.br().y)+10)},
                        cv::FONT_HERSHEY_SIMPLEX,.38,color,1,cv::LINE_AA);
        }
        writer.write(frame);
    } while(capture.read(frame));
    writer.release();
    log.close();
    std::sort(elapsed.begin(),elapsed.end());
    std::sort(trackingMs.begin(),trackingMs.end());
    std::ofstream summary(out/"summary.json");
    summary.exceptions(std::ios::badbit|std::ios::failbit);
    summary<<"{\"frames\":"<<id<<",\"overlay_fps\":"<<fps
           <<",\"mot_p50_ms\":"<<trackingMs[(trackingMs.size()-1)/2]
           <<",\"mot_p95_ms\":"<<trackingMs[static_cast<size_t>(std::ceil(.95*trackingMs.size()))-1]
           <<",\"detect_p50_ms\":"<<elapsed[(elapsed.size()-1)/2]
           <<",\"detect_p95_ms\":"<<elapsed[static_cast<size_t>(std::ceil(.95*elapsed.size()))-1]<<"}\n";
    summary.close();
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<"\n";
    return 1;
}


