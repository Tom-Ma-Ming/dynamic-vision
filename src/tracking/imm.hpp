#pragma once
#include <array>
#include <opencv2/core.hpp>

namespace mot {
struct ImmConfig {
    bool enabled = false;
    double stationaryAcceleration = 30;
    double maneuverAcceleration = 900;
    double stationaryTau = .08;
    double switchRate = 1;
    double gateProbability = .05;
};
struct GaussianState {
    cv::Vec4d mean{};
    cv::Matx44d covariance{};
};
struct ImmState {
    std::array<GaussianState,3> models;
    std::array<double,3> probabilities{{1./3,1./3,1./3}};
    void initialize(const cv::Vec4d& mean,const cv::Matx44d& covariance);
    void predict(double dt,double cvAcceleration,const ImmConfig& config);
    void update(cv::Vec2d observation,double variance);
    GaussianState fused() const;
    double gateDistance(cv::Vec2d observation,double variance,double minimumProbability) const;
    void translate(cv::Point2f offset);
};
}
