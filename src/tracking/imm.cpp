#include "imm.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mot {
namespace {
const cv::Matx<double,2,4> H(1,0,0,0,0,1,0,0);
cv::Matx44d outer(const cv::Vec4d& x) {
    return x*x.t();
}
struct Innovation {
    cv::Vec2d residual;
    cv::Matx22d inverse;
    double determinant;
    double distance;
};
Innovation innovation(const GaussianState& model,cv::Vec2d z,double variance) {
    auto residual = z-H*model.mean;
    auto s = H*model.covariance*H.t()+cv::Matx22d::eye()*variance;
    double det = cv::determinant(s);
    if(!std::isfinite(det) || det<=0)
        throw std::runtime_error("Invalid IMM innovation covariance");
    auto inverse=s.inv(cv::DECOMP_CHOLESKY);
    return {residual,inverse,det,residual.dot(inverse*residual)};
}
}

void ImmState::initialize(const cv::Vec4d& mean,const cv::Matx44d& covariance) {
    probabilities={{1./3,1./3,1./3}};
    for(auto& model:models) model={mean,covariance};
}

GaussianState ImmState::fused() const {
    GaussianState result;
    for(size_t j=0;j<models.size();++j) result.mean+=probabilities[j]*models[j].mean;
    for(size_t j=0;j<models.size();++j) {
        auto delta=models[j].mean-result.mean;
        result.covariance+=probabilities[j]*(models[j].covariance+outer(delta));
    }
    return result;
}

void ImmState::predict(double dt,double cvAcceleration,const ImmConfig& config) {
    if(dt<0 || !std::isfinite(dt)) throw std::invalid_argument("Invalid IMM prediction interval");
    if(dt==0) return;
    // Matrix exponential of a symmetric CTMC with off-diagonal rate switchRate.
    double off=(1-std::exp(-3*config.switchRate*dt))/3;
    double diagonal=1-2*off;
    std::array<GaussianState,3> mixed{};
    std::array<double,3> prior{};
    for(size_t j=0;j<3;++j) {
        for(size_t i=0;i<3;++i) prior[j]+=probabilities[i]*(i==j?diagonal:off);
        for(size_t i=0;i<3;++i) {
            double weight=probabilities[i]*(i==j?diagonal:off)/prior[j];
            mixed[j].mean+=weight*models[i].mean;
        }
        for(size_t i=0;i<3;++i) {
            double weight=probabilities[i]*(i==j?diagonal:off)/prior[j];
            auto delta=models[i].mean-mixed[j].mean;
            mixed[j].covariance+=weight*(models[i].covariance+outer(delta));
        }
    }
    const std::array<double,3> noise{{config.stationaryAcceleration,cvAcceleration,config.maneuverAcceleration}};
    for(size_t j=0;j<3;++j) {
        auto f=cv::Matx44d::eye();
        double positionVelocity=dt;
        double velocityRetention=1;
        if(j==0) {
            velocityRetention=std::exp(-dt/config.stationaryTau);
            positionVelocity=config.stationaryTau*(1-velocityRetention);
        }
        f(0,2)=positionVelocity; f(1,3)=positionVelocity;
        f(2,2)=velocityRetention; f(3,3)=velocityRetention;
        // Piecewise constant acceleration during this prediction interval.
        double positionAcceleration=dt*dt/2, velocityAcceleration=dt;
        if(j==0) {
            velocityAcceleration=positionVelocity;
            positionAcceleration=config.stationaryTau*(dt-positionVelocity);
        }
        cv::Matx<double,4,2> g(positionAcceleration,0,0,positionAcceleration,
                               velocityAcceleration,0,0,velocityAcceleration);
        models[j].mean=f*mixed[j].mean;
        models[j].covariance=f*mixed[j].covariance*f.t()+noise[j]*noise[j]*(g*g.t());
    }
    probabilities=prior;
}

void ImmState::update(cv::Vec2d observation,double variance) {
    std::array<double,3> logWeights{};
    for(size_t j=0;j<3;++j) {
        auto c=innovation(models[j],observation,variance);
        logWeights[j]=std::log(probabilities[j])-.5*(c.distance+std::log(c.determinant));
        auto k=models[j].covariance*H.t()*c.inverse;
        auto a=cv::Matx44d::eye()-k*H;
        models[j].mean+=k*c.residual;
        auto covariance=a*models[j].covariance*a.t()+variance*(k*k.t());
        models[j].covariance=(covariance+covariance.t())*.5;
    }
    double largest=*std::max_element(logWeights.begin(),logWeights.end()), total=0;
    for(size_t j=0;j<3;++j) {
        probabilities[j]=std::max(1e-15,std::exp(logWeights[j]-largest));
        total+=probabilities[j];
    }
    for(auto& p:probabilities) p/=total;
}

double ImmState::gateDistance(cv::Vec2d z,double variance,double minimumProbability) const {
    double best=std::numeric_limits<double>::infinity();
    for(size_t j=0;j<3;++j) {
        if(probabilities[j]>=minimumProbability)
            best=std::min(best,innovation(models[j],z,variance).distance);
    }
    return best;
}

void ImmState::translate(cv::Point2f offset) {
    for(auto& model:models) {model.mean[0]+=offset.x; model.mean[1]+=offset.y;}
}
}
