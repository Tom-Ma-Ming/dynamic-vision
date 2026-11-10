#include "yolo_onnx.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace vision {
namespace {

std::vector<char> readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open YOLO ONNX model: " + path);
  return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}

struct YoloOnnx::State {
  std::vector<char> model;
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "yolo"};
  Ort::SessionOptions options;
  std::optional<Ort::Session> session;
  Ort::AllocatorWithDefaultOptions allocator;
  std::string input_name;
  std::string output_name;
  std::vector<float> input;
  bool channel_major = true;
};

YoloOnnx::~YoloOnnx() = default;

YoloOnnx::YoloOnnx(const std::string& path) : path_(path), state_(std::make_unique<State>()) {
  state_->model = readFile(path);
  if (state_->model.empty()) throw std::runtime_error("YOLO ONNX model is empty: " + path);
  state_->options.SetIntraOpNumThreads(1);
  state_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
  state_->session.emplace(state_->env, state_->model.data(), state_->model.size(), state_->options);
  if (state_->session->GetInputCount() < 1 || state_->session->GetOutputCount() < 1)
    throw std::runtime_error("YOLO ONNX model has no input or output");
  state_->input_name = state_->session->GetInputNameAllocated(0, state_->allocator).get();
  state_->output_name = state_->session->GetOutputNameAllocated(0, state_->allocator).get();
  auto input_type = state_->session->GetInputTypeInfo(0);
  auto input_info = input_type.GetTensorTypeAndShapeInfo();
  const auto input_shape = input_info.GetShape();
  if (input_shape.size() != 4 || input_shape[1] != 3 || input_shape[2] <= 0 || input_shape[2] != input_shape[3]) {
    std::string message = "YOLO ONNX input must be NCHW square RGB, got";
    for (auto dim : input_shape) message += " " + std::to_string(dim);
    throw std::runtime_error(message);
  }
  input_size_ = static_cast<int>(input_shape[2]);
  state_->input.assign(static_cast<size_t>(3) * static_cast<size_t>(input_size_) * static_cast<size_t>(input_size_), 0.f);
  auto output_type = state_->session->GetOutputTypeInfo(0);
  auto output_info = output_type.GetTensorTypeAndShapeInfo();
  const auto output_shape = output_info.GetShape();
  if (output_shape.size() != 3 || output_shape[0] != 1)
    throw std::runtime_error("YOLO ONNX output must be [1, channels, anchors] or [1, anchors, channels]");
  const int first = static_cast<int>(output_shape[1]);
  const int second = static_cast<int>(output_shape[2]);
  if (first <= 4 || second <= 4) throw std::runtime_error("YOLO ONNX output rank is too small");
  // The class/box axis is the smaller one. Ultralytics exports channel-major.
  state_->channel_major = first < second;
  channels_ = state_->channel_major ? first : second;
  anchors_ = state_->channel_major ? second : first;
}

std::vector<YoloBox> YoloOnnx::detect(const cv::Mat& bgr, float confidence, float iou) {
  if (bgr.empty() || bgr.type() != CV_8UC3) throw std::runtime_error("YOLO expects a BGR image");
  const Letterbox letter(bgr.size(), input_size_);
  cv::Mat canvas(input_size_, input_size_, CV_8UC3, cv::Scalar(114, 114, 114));
  cv::Mat region = canvas(cv::Rect(letter.left, letter.top, letter.width, letter.height));
  cv::resize(bgr, region, region.size());
  cv::cvtColor(canvas, canvas, cv::COLOR_BGR2RGB);
  cv::Mat floats;
  canvas.convertTo(floats, CV_32F, 1.0 / 255.0);
  cv::Mat planes[3] = {
      cv::Mat(input_size_, input_size_, CV_32F, state_->input.data()),
      cv::Mat(input_size_, input_size_, CV_32F, state_->input.data() + input_size_ * input_size_),
      cv::Mat(input_size_, input_size_, CV_32F, state_->input.data() + 2 * input_size_ * input_size_),
  };
  cv::split(floats, planes);
  const int64_t shape[4] = {1, 3, input_size_, input_size_};
  Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value input = Ort::Value::CreateTensor<float>(memory, state_->input.data(), state_->input.size(), shape, 4);
  const char* input_names[] = {state_->input_name.c_str()};
  const char* output_names[] = {state_->output_name.c_str()};
  auto outputs = state_->session->Run(Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);
  const float* raw = outputs.front().GetTensorData<float>();
  std::vector<float> major;
  const float* channel_major = raw;
  if (!state_->channel_major) {
    major.resize(static_cast<size_t>(channels_) * anchors_);
    for (int channel = 0; channel < channels_; ++channel)
      for (int anchor = 0; anchor < anchors_; ++anchor)
        major[channel * anchors_ + anchor] = raw[anchor * channels_ + channel];
    channel_major = major.data();
  }
  return decodeYolo(channel_major, channels_, anchors_, bgr.size(), letter, confidence, iou, person_channel_);
}

}  // namespace vision
