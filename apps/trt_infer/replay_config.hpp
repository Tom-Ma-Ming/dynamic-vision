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

  static ReplayConfig load(const std::string& path) {
    cv::FileStorage file(path, cv::FileStorage::READ);
    if (!file.isOpened()) {
      throw std::runtime_error("Cannot open replay config: " + path);
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
