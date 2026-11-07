#pragma once

#include <cstdint>
#include <opencv2/core.hpp>

namespace replay {

// One decoded frame handed from the capture producer to the detect/track
// consumer. image owns its pixels; the producer must not reuse the buffer after
// pushing.
struct FramePacket {
  std::uint64_t frame_id = 0;
  cv::Mat image;
  std::int64_t source_ts_ns = 0;     // media PTS used by the motion timeline
  std::int64_t capture_steady_ns = 0;  // when decode returned this frame
  std::int64_t enqueue_steady_ns = 0;  // just before the queue push
  bool timestamp_from_pts = false;     // false means FPS fallback was used
};

}  // namespace replay
