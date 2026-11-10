#pragma once

#include "vision/detect/YoloPostprocess.hpp"

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace vision {

// Runs a real YOLO ONNX graph. This is not TensorRT and not a color threshold.
// A 1x6xN graph is the project head/person contract. A wider graph is COCO-style
// and only the person channel is returned, relabeled as project class 1.
class YoloOnnx {
 public:
  explicit YoloOnnx(const std::string& path);
  ~YoloOnnx();
  std::vector<YoloBox> detect(const cv::Mat& bgr, float confidence = 0.25f, float iou = 0.45f);
  const std::string& path() const { return path_; }
  int input_size() const { return input_size_; }
  int channels() const { return channels_; }
  int anchors() const { return anchors_; }
  bool project_contract() const { return channels_ == 6; }

 private:
  std::string path_;
  int input_size_ = 320;
  int channels_ = 0;
  int anchors_ = 0;
  int person_channel_ = 0;
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace vision
