#pragma once

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vision {

// Project class ids. A COCO graph has no head class; its person channel is
// emitted as person (1) so the existing tracker can select a body.
struct YoloBox {
  int cls = 1;
  float score = 0;
  cv::Rect2f box;
};

// Same letterbox as apps/trt_infer/image_detection.hpp: scale the long side
// onto the canvas and pad with gray. Coordinates below are in that canvas.
struct Letterbox {
  int canvas = 320;
  int width = 320;
  int height = 320;
  int left = 0;
  int top = 0;
  explicit Letterbox(cv::Size size, int canvas_px = 320) : canvas(canvas_px) {
    const double scale = double(canvas) / std::max(size.width, size.height);
    width = std::max(1, cvRound(size.width * scale));
    height = std::max(1, cvRound(size.height * scale));
    left = (canvas - width) / 2;
    top = (canvas - height) / 2;
  }
  cv::Rect2f restore(cv::Rect2f box, cv::Size source) const {
    const float sx = float(source.width) / float(width);
    const float sy = float(source.height) / float(height);
    const cv::Rect2f restored((box.x - left) * sx, (box.y - top) * sy, box.width * sx, box.height * sy);
    return restored & cv::Rect2f(0, 0, float(source.width), float(source.height));
  }
};

// `raw` is channel-major: index = channel * anchors + anchor.
// Six channels use the project head/person contract (argmax of channels 4 and 5).
// Any wider YOLO graph keeps only `person_channel` (COCO person is 0) and
// labels it as project class 1.
inline std::vector<YoloBox> decodeYolo(const float* raw, int channels, int anchors, cv::Size source,
                                       const Letterbox& letter, float confidence, float iou,
                                       int person_channel = 0) {
  std::vector<YoloBox> candidates;
  const bool project = channels == 6;
  if (!project && (person_channel < 0 || 4 + person_channel >= channels)) return {};
  for (int i = 0; i < anchors; ++i) {
    const float width = raw[2 * anchors + i];
    const float height = raw[3 * anchors + i];
    if (!(width > 0.f && height > 0.f)) continue;
    int cls = 1;
    float score = 0;
    if (project) {
      cls = raw[5 * anchors + i] > raw[4 * anchors + i] ? 1 : 0;
      score = raw[(4 + cls) * anchors + i];
    } else {
      score = raw[(4 + person_channel) * anchors + i];
    }
    if (score >= confidence) {
      const float cx = raw[i];
      const float cy = raw[anchors + i];
      candidates.push_back({cls, score, {cx - width * 0.5f, cy - height * 0.5f, width, height}});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                    [](const YoloBox& a, const YoloBox& b) { return a.score > b.score; });
  std::vector<YoloBox> kept;
  for (const auto& candidate : candidates) {
    const bool suppress = std::any_of(kept.begin(), kept.end(), [&](const YoloBox& prior) {
      const float common = (prior.box & candidate.box).area();
      const float union_area = prior.box.area() + candidate.box.area() - common;
      return prior.cls == candidate.cls && union_area > 0.f && common > iou * union_area;
    });
    if (!suppress) kept.push_back(candidate);
  }
  std::vector<YoloBox> restored;
  for (auto box : kept) {
    box.box = letter.restore(box.box, source);
    if (box.box.width > 0.f && box.box.height > 0.f) restored.push_back(box);
  }
  return restored;
}

}  // namespace vision
