#pragma once
#include <memory>
#include <vector>

class Detector {
public:
    explicit Detector(const char* enginePath);
    ~Detector();
    std::vector<float> infer(const void* nchw);
private:
    struct State;
    std::unique_ptr<State> state;
};
