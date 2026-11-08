#pragma once

#include <opencv2/core.hpp>
#include <opencv2/core/persistence.hpp>
#include <stdexcept>
#include <string>

#include "vision/core/BoundedQueue.hpp"

namespace replay {

struct ReplayConfig {
  int queue_capacity = 1;
  vision::QueueMode queue_mode = vision::QueueMode::Blocking;
  bool realtime_pace = false;
  int log_chunk_bytes = 0;
  int log_keep_files = 4;
  bool write_overlay = true;

  static ReplayConfig load(const std::string& path) {
    cv::FileStorage file(path, cv::FileStorage::READ);
    if (!file.isOpened()) {
      throw std::runtime_error("Cannot open replay config: " + path);
    }
    const cv::FileNode root = file.root();
    for (auto it = root.begin(); it != root.end(); ++it) {
      const std::string key = (*it).name();
      if (key != "queue_capacity" && key != "queue_mode" &&
          key != "realtime_pace" && key != "log_chunk_bytes" &&
          key != "log_keep_files" && key != "write_overlay") {
        throw std::runtime_error("Unknown replay setting: " + key);
      }
    }
    for (const char* key : {"queue_capacity", "queue_mode", "realtime_pace"}) {
      if (file[key].empty()) {
        throw std::runtime_error(std::string("Missing replay setting: ") + key);
      }
    }

    ReplayConfig config;
    config.queue_capacity = static_cast<int>(file["queue_capacity"]);
    const std::string mode = static_cast<std::string>(file["queue_mode"]);
    if (mode == "blocking") {
      config.queue_mode = vision::QueueMode::Blocking;
    } else if (mode == "latest") {
      config.queue_mode = vision::QueueMode::Latest;
    } else {
      throw std::runtime_error("queue_mode must be blocking or latest");
    }
    config.realtime_pace = static_cast<int>(file["realtime_pace"]) != 0;
    for(const char* key : {"log_chunk_bytes","log_keep_files","write_overlay"}) {
      if(!file[key].empty() && !file[key].isInt()) throw std::runtime_error(std::string(key)+" must be an integer");
    }
    if(!file["log_chunk_bytes"].empty()) config.log_chunk_bytes=int(file["log_chunk_bytes"]);
    if(!file["log_keep_files"].empty()) config.log_keep_files=int(file["log_keep_files"]);
    if(!file["write_overlay"].empty()) {
      const int value=int(file["write_overlay"]);
      if(value!=0 && value!=1) throw std::runtime_error("write_overlay must be 0 or 1");
      config.write_overlay=value!=0;
    }
    if(config.log_chunk_bytes!=0 && (config.log_chunk_bytes<1024 || config.log_chunk_bytes>64*1024*1024))
      throw std::runtime_error("log_chunk_bytes must be 0 or 1024..67108864");
    if(config.log_keep_files<1 || config.log_keep_files>100)
      throw std::runtime_error("log_keep_files must be 1..100");

    if (config.queue_capacity < 1 || config.queue_capacity > 2) {
      throw std::runtime_error("queue_capacity must be 1 or 2");
    }
    if (config.realtime_pace && config.queue_mode != vision::QueueMode::Latest) {
      throw std::runtime_error("realtime_pace requires queue_mode: latest");
    }
    return config;
  }
};

}  // namespace replay
