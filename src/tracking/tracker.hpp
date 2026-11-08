#pragma once
#include <opencv2/core.hpp>
#include "imm.hpp"
#include "camera_motion.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace mot {
using Id = std::uint64_t;
using Time = std::int64_t;
struct Detection { int cls; float score; cv::Rect2f box; };
struct Config {
    int minHits;
    double maxLostMs, birthScore, sigma, headSigma, accel, initialSpeed, gate, iouWeight, ambiguity;
    ImmConfig imm;
    CameraMotionConfig cameraMotion;
    bool separateHeadAmbiguity = false;
    bool byteTrack = false;
    double highScore = 0.4;
    double lowScore = 0.1;
    static Config load(const std::string& path);
};
enum class State { Tentative, Confirmed, Lost };
struct Track {
    Id id;
    State state = State::Tentative;
    cv::Vec4d x{};
    cv::Matx44d covariance{};
    std::optional<ImmState> imm;
    cv::Size2f size;
    cv::Point2f headOffset{};
    bool body = false, ambiguous = false, headAmbiguous = false;
    // Timestamp of the last unambiguous associated head measurement, not a prediction.
    std::optional<Time> lastHeadSeen;
    bool headMeasuredAt(Time timestamp) const {
        return lastHeadSeen && *lastHeadSeen==timestamp && !ambiguous &&
               !headAmbiguous && state!=State::Lost;
    }
    Time lastSeen;
    int hits = 1;
    double score, cost = -1, innovation = -1;
    cv::Rect2f box() const;
};
struct Snapshot {
    Time frame = -1, timestamp = -1;
    std::vector<Track> tracks;
    std::vector<Id> removed;
};
class Tracker {
public:
    explicit Tracker(Config config) : cfg(config) {}
    bool update(Time frame, Time timestamp, const std::vector<Detection>& detections,
                const CameraMotionEstimate* camera=nullptr);
    Snapshot predictAt(Time timestamp) const;
    const Snapshot& snapshot() const { return current; }
private:
    Config cfg;
    Snapshot current;
    Id nextId=1;
};
class Selection {
public:
    bool select(Id id,const Snapshot& snapshot);
    void clear() { selected.reset(); }
    const char* state(const Snapshot& snapshot) const;
    std::optional<Id> selected;
};
const char* name(State state);
}
