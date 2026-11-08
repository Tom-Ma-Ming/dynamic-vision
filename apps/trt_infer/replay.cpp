#include "frame_packet.hpp"
#include "image_detection.hpp"
#include "replay_config.hpp"
#include "tracking_output.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "vision/core/BoundedQueue.hpp"
#include "vision/core/LatencyStats.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

std::int64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

double milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

void usage() {
  std::cout
      << "usage: vision_replay engine video new-output-directory "
         "[tracker.yml] [selection.txt] [--config replay.yml]\n";
}

struct Args {
  std::string engine;
  std::string video;
  std::string output;
  std::string tracker;
  std::string selection;
  std::string replay_config;
};

std::optional<Args> parseArgs(int argc, char** argv) {
  std::vector<std::string> positional;
  std::string config;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--help") {
      usage();
      return std::nullopt;
    }
    if (argument == "--config") {
      if (++i >= argc) throw std::runtime_error("--config requires a path");
      config = argv[i];
      continue;
    }
    if (!argument.empty() && argument[0] == '-') {
      throw std::runtime_error("Unknown option: " + argument);
    }
    positional.push_back(argument);
  }
  if (positional.size() < 3 || positional.size() > 5) {
    throw std::runtime_error(
        "usage: vision_replay engine video new-output-directory "
        "[tracker.yml] [selection.txt] [--config replay.yml]");
  }

  const fs::path executable_dir = fs::absolute(argv[0]).parent_path();
  Args args;
  args.engine = positional[0];
  args.video = positional[1];
  args.output = positional[2];
  args.tracker = positional.size() >= 4
                     ? positional[3]
                     : (executable_dir / "tracker.yml").string();
  args.selection = positional.size() == 5 ? positional[4] : "";
  args.replay_config = config.empty()
                           ? (executable_dir / "replay.yml").string()
                           : config;
  return args;
}

std::map<mot::Time, mot::Id> loadSelectionCommands(const std::string& path) {
  std::map<mot::Time, mot::Id> commands;
  if (path.empty()) return commands;

  std::ifstream file(path);
  if (!file) throw std::runtime_error("Cannot open selection commands");
  mot::Time frameId;
  mot::Id trackId;
  while (file >> std::ws && file.peek() != EOF) {
    if (!(file >> frameId >> trackId) || frameId < 0 ||
        !commands.emplace(frameId, trackId).second) {
      throw std::runtime_error(
          "Expected unique frame_id track_id selection pairs");
    }
  }
  return commands;
}

std::string latencyJson(const vision::LatencySummary& summary) {
  std::ostringstream out;
  out.precision(9);
  out << "{\"p50\":" << vision::nsToMs(summary.p50_ns)
      << ",\"p95\":" << vision::nsToMs(summary.p95_ns)
      << ",\"p99\":" << vision::nsToMs(summary.p99_ns)
      << ",\"max\":" << vision::nsToMs(summary.max_ns)
      << ",\"n\":" << summary.n << "}";
  return out.str();
}

std::string jsonString(const std::string& value) {
  std::string escaped = "\"";
  for (const char character : value) {
    switch (character) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += character; break;
    }
  }
  escaped += '"';
  return escaped;
}

std::string errorMessage(const std::exception_ptr& error) {
  if (!error) return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& exception) {
    return exception.what();
  } catch (...) {
    return "unknown error";
  }
}

}  // namespace

int main(int argc, char** argv) try {
  const auto args = parseArgs(argc, argv);
  if (!args) return 0;

  const replay::ReplayConfig config =
      replay::ReplayConfig::load(args->replay_config);

  cv::VideoCapture capture(args->video);
  if (!capture.isOpened()) throw std::runtime_error("Cannot open input video");
  const double fps = capture.get(cv::CAP_PROP_FPS);
  if (!std::isfinite(fps) || fps <= 0) {
    throw std::runtime_error("Missing video FPS");
  }

  const mot::Config tracker_config = mot::Config::load(args->tracker);
  mot::Tracker tracker(tracker_config);
  mot::CameraMotionEstimator camera_estimator(tracker_config.cameraMotion);
  mot::Selection selection;
  const auto commands = loadSelectionCommands(args->selection);

  Detector detector(args->engine.c_str());

  const fs::path output(args->output);
  if (!fs::create_directory(output)) {
    throw std::runtime_error("Use a new output directory");
  }
  std::ofstream log(output / "frames.jsonl");
  log.exceptions(std::ios::badbit | std::ios::failbit);

  vision::BoundedQueue<replay::FramePacket> queue(
      static_cast<std::size_t>(config.queue_capacity), config.queue_mode);

  std::atomic<std::uint64_t> captured{0};
  std::atomic<std::uint64_t> processed{0};
  std::atomic<std::uint64_t> rejected{0};
  std::atomic<std::uint64_t> stale{0};
  std::atomic<std::uint64_t> failed{0};
  std::exception_ptr producer_error;
  std::exception_ptr consumer_error;

  std::vector<std::int64_t> queue_wait_ns;
  std::vector<std::int64_t> infer_ns;
  std::vector<std::int64_t> mot_ns;
  std::vector<std::int64_t> camera_ns;
  std::vector<std::int64_t> result_age_ns;

  const auto pacing_start = Clock::now();
  std::int64_t first_source_ts = 0;

  const auto producer = [&] {
    try {
      cv::Mat frame;
      std::uint64_t frame_id = 0;
      std::int64_t last_timestamp = -1;
      bool have_pacing_origin = false;

      while (capture.read(frame)) {
        const double pts_ms = capture.get(cv::CAP_PROP_POS_MSEC);
        const bool pts_valid = std::isfinite(pts_ms) && pts_ms >= 0;
        std::int64_t source_ts =
            pts_valid ? std::llround(pts_ms * 1e6) : -1;
        const bool monotonic = pts_valid && source_ts > last_timestamp;
        if (!monotonic) {
          source_ts = last_timestamp < 0
                          ? 0
                          : last_timestamp + std::llround(1e9 / fps);
        }
        last_timestamp = source_ts;

        if (config.realtime_pace) {
          if (!have_pacing_origin) {
            first_source_ts = source_ts;
            have_pacing_origin = true;
          }
          const auto target =
              pacing_start + std::chrono::nanoseconds(source_ts - first_source_ts);
          if (!queue.waitUntil(target)) break;
        }

        replay::FramePacket packet;
        packet.frame_id = frame_id++;
        packet.image = frame.clone();
        packet.source_ts_ns = source_ts;
        packet.timestamp_from_pts = monotonic;
        packet.capture_steady_ns = nowNs();
        packet.enqueue_steady_ns = packet.capture_steady_ns;

        ++captured;
        if (!queue.push(std::move(packet))) {
          ++rejected;
          break;
        }
      }
    } catch (...) {
      producer_error = std::current_exception();
      ++failed;
    }
    queue.close();
  };

  const auto consumer = [&] {
    try {
      cv::VideoWriter writer;
      bool writer_open = false;

      while (auto packet = queue.pop()) {
        const std::int64_t dequeue_ns = nowNs();
        auto& frame = packet->image;

        const auto infer_start = Clock::now();
        const float detect_confidence =
            tracker_config.byteTrack ? static_cast<float>(tracker_config.lowScore) : .25f;
        const auto detections = detect(detector, frame, detect_confidence, .45f);
        const auto infer_end = Clock::now();

        std::vector<mot::Detection> observations;
        observations.reserve(detections.size());
        for (const auto& detection : detections) {
          observations.push_back(
              {detection.cls, detection.score, detection.box});
        }

        mot::CameraMotionEstimate camera;
        std::int64_t camera_elapsed=0;
        if(tracker_config.cameraMotion.enabled) {
          std::vector<cv::Rect2f> foreground;
          for(const auto& observation:observations) foreground.push_back(observation.box);
          const auto camera_start=Clock::now();
          camera=camera_estimator.update(frame,packet->source_ts_ns,foreground);
          camera_elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-camera_start).count();
        }
        const auto track_start = Clock::now();
        if (!tracker.update(static_cast<mot::Time>(packet->frame_id),
                            packet->source_ts_ns, observations,
                            tracker_config.cameraMotion.enabled ? &camera : nullptr)) {
          ++stale;
          continue;
        }
        const auto track_end = Clock::now();
        const auto& snapshot = tracker.snapshot();

        const auto command =
            commands.find(static_cast<mot::Time>(packet->frame_id));
        if (command != commands.end()) {
          if (!command->second) {
            selection.clear();
          } else if (!selection.select(command->second, snapshot)) {
            std::cerr << "Selection unavailable at frame "
                      << packet->frame_id << "\n";
          }
        }

        const std::int64_t publish_ns = nowNs();
        const std::int64_t queue_wait = dequeue_ns - packet->enqueue_steady_ns;
        const std::int64_t infer =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                infer_end - infer_start)
                .count();
        const std::int64_t mot =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                track_end - track_start)
                .count();
        const std::int64_t result_age =
            publish_ns - packet->capture_steady_ns;

        log << "{\"frame_id\":" << packet->frame_id
            << ",\"source_ts_ns\":" << packet->source_ts_ns
            << ",\"timestamp_source\":\""
            << (packet->timestamp_from_pts ? "video_pts" : "fps_estimate")
            << "\",\"capture_steady_ns\":" << packet->capture_steady_ns
            << ",\"publish_steady_ns\":" << publish_ns
            << ",\"queue_wait_ms\":" << vision::nsToMs(queue_wait)
            << ",\"infer_ms\":" << vision::nsToMs(infer)
            << ",\"mot_ms\":" << vision::nsToMs(mot)
            << ",\"result_age_ms\":" << vision::nsToMs(result_age)
            << ",\"detect_ms\":" << vision::nsToMs(infer)
            << ",\"detections\":" << boxesJson(detections);
        writeTracking(log, snapshot, selection);
        if(tracker_config.cameraMotion.enabled) {
          log << ",\"camera_ms\":" << vision::nsToMs(camera_elapsed)
              << ",\"camera_motion\":{\"accepted\":" << (camera.accepted ? "true" : "false")
              << ",\"reason\":" << jsonString(camera.reason)
              << ",\"from_ts_ns\":" << camera.from << ",\"to_ts_ns\":" << camera.to
              << ",\"dx_px\":" << camera.shift.x << ",\"dy_px\":" << camera.shift.y
              << ",\"position_variance_px2\":" << camera.variance
              << ",\"features\":" << camera.features << ",\"inliers\":" << camera.inliers
              << ",\"inlier_ratio\":" << camera.inlierRatio << ",\"coverage\":" << camera.coverage
              << ",\"grid_cells\":" << camera.gridCells << "}";
          camera_ns.push_back(camera_elapsed);
        }
        log << "}\n";

        ++processed;
        queue_wait_ns.push_back(queue_wait);
        infer_ns.push_back(infer);
        mot_ns.push_back(mot);
        result_age_ns.push_back(result_age);

        if (!writer_open) {
          if (fs::is_directory(output / "overlay.mp4"))
            throw std::runtime_error("Cannot open overlay writer");
          writer.open((output / "overlay.mp4").string(),
                      cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps,
                      frame.size());
          if (!writer.isOpened()) {
            throw std::runtime_error("Cannot open overlay writer");
          }
          writer_open = true;
        }
        for (const auto& detection : detections) {
          const auto color = detection.cls == 0 ? cv::Scalar(0, 255, 255)
                                                : cv::Scalar(255, 255, 0);
          cv::rectangle(frame, detection.box, color, 1);
          cv::putText(frame, detection.cls == 0 ? "head" : "person",
                      {cvRound(detection.box.x),
                       std::max(10, cvRound(detection.box.y) - 2)},
                      cv::FONT_HERSHEY_SIMPLEX, .35, color, 1, cv::LINE_AA);
        }
        for (const auto& track : snapshot.tracks) {
          if (track.state == mot::State::Tentative) continue;
          const auto box = track.box();
          const auto color = selection.selected == track.id
                                 ? cv::Scalar(80, 80, 255)
                                 : cv::Scalar(80, 230, 120);
          cv::rectangle(frame, box, color, 1);
          std::string label = "#" + std::to_string(track.id);
          if (track.state == mot::State::Lost) label += " lost";
          if (track.ambiguous) label += " ?";
          if (track.headAmbiguous) label += " head?";
          else if (track.headMeasuredAt(snapshot.timestamp)) label += " head+";
          else label += " head-";
          cv::putText(frame, label,
                      {cvRound(box.x), std::max(12, cvRound(box.br().y) + 10)},
                      cv::FONT_HERSHEY_SIMPLEX, .38, color, 1, cv::LINE_AA);
        }
        writer.write(frame);
      }
      if (writer_open) writer.release();
    } catch (...) {
      consumer_error = std::current_exception();
      ++failed;
      queue.close();
    }
  };

  std::thread producer_thread(producer);
  std::thread consumer_thread;
  try {
    consumer_thread = std::thread(consumer);
  } catch (...) {
    queue.close();
    producer_thread.join();
    throw;
  }
  producer_thread.join();
  consumer_thread.join();
  queue.close();
  log.exceptions(std::ios::goodbit);
  log.close();

  const auto queue_wait_summary = vision::summarize(queue_wait_ns);
  const auto infer_summary = vision::summarize(infer_ns);
  const auto mot_summary = vision::summarize(mot_ns);
  const auto age_summary = vision::summarize(result_age_ns);

  const std::uint64_t processed_count = processed.load();
  const bool empty_output = processed_count == 0;
  const bool has_error = producer_error || consumer_error || empty_output;
  const std::string status = has_error ? "error" : "ok";
  const std::string error =
      producer_error ? errorMessage(producer_error)
                     : consumer_error ? errorMessage(consumer_error)
                                      : (empty_output ? "Empty or unreadable video" : "");

  std::ofstream summary(output / "summary.json");
  summary.exceptions(std::ios::badbit | std::ios::failbit);
  summary << "{\"frames\":" << processed_count
          << ",\"captured\":" << captured.load()
          << ",\"processed\":" << processed_count
          << ",\"replaced\":" << queue.replaced()
          << ",\"rejected\":" << rejected.load()
          << ",\"pending\":" << queue.size()
          << ",\"stale\":" << stale.load()
          << ",\"failed\":" << failed.load()
          << ",\"capacity\":" << queue.capacity()
          << ",\"mode\":\"" << vision::queueModeName(queue.mode()) << "\""
          << ",\"paced\":" << (config.realtime_pace ? "true" : "false")
          << ",\"peak\":" << queue.peak()
          << ",\"overlay_fps\":" << fps
          << ",\"queue_wait_ms\":" << latencyJson(queue_wait_summary)
          << ",\"infer_ms\":" << latencyJson(infer_summary)
          << ",\"mot_ms\":" << latencyJson(mot_summary)
          << ",\"result_age_ms\":" << latencyJson(age_summary)
          << ",\"detect_p50_ms\":" << vision::nsToMs(infer_summary.p50_ns)
          << ",\"detect_p95_ms\":" << vision::nsToMs(infer_summary.p95_ns)
          << ",\"mot_p50_ms\":" << vision::nsToMs(mot_summary.p50_ns)
          << ",\"mot_p95_ms\":" << vision::nsToMs(mot_summary.p95_ns)
          << ",\"status\":\"" << status << "\""
          << ",\"error\":" << (error.empty() ? "null" : jsonString(error));
  if(tracker_config.cameraMotion.enabled)
    summary << ",\"camera_ms\":" << latencyJson(vision::summarize(camera_ns));
  summary << "}\n";
  summary.close();

  if (producer_error) std::rethrow_exception(producer_error);
  if (consumer_error) std::rethrow_exception(consumer_error);
  if (processed_count == 0) {
    throw std::runtime_error("Empty or unreadable video");
  }
  return 0;
} catch (const std::exception& exception) {
  std::cerr << exception.what() << "\n";
  return 1;
}
