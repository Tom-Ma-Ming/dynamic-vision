#pragma once
#include <opencv2/core.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace mot {
struct CameraMotionConfig {
    bool enabled=false;
    int maxCorners=300, minInliers=20, minGridCells=4;
    double maxFbError=1.5, residualThreshold=2., minInlierRatio=.65;
    double minCoverage=.15, maxShiftFraction=.25, maxGapMs=200.;
    double foregroundMargin=8., borderFraction=.05, centerRadius=12.;
    void validate() const;
};
struct CameraMotionEstimate {
    std::int64_t from=-1, to=-1;
    bool accepted=false;
    cv::Point2d shift{};
    double variance=0, inlierRatio=0, coverage=0;
    int features=0, inliers=0, gridCells=0;
    std::string reason="initial";
};
class CameraMotionEstimator {
public:
    explicit CameraMotionEstimator(CameraMotionConfig config) : cfg(config) { cfg.validate(); }
    CameraMotionEstimate update(const cv::Mat& image,std::int64_t timestamp,
                                const std::vector<cv::Rect2f>& foreground);
private:
    CameraMotionConfig cfg;
    cv::Mat previousGray,previousMask;
    std::int64_t previousTimestamp=-1;
};
}
