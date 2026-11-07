#pragma once
#include <memory>

class Detector {
public:
    static constexpr int kInputFloats = 3 * 320 * 320;
    static constexpr int kOutputFloats = 6 * 2100;

    explicit Detector(const char* enginePath);
    ~Detector();

    // Pinned host input buffer. Write exactly kInputFloats floats before infer().
    float* input();

    // Runs on the current input() buffer and returns the pinned host output.
    // The pointer is valid until the next infer() call.
    const float* infer();

private:
    struct State;
    std::unique_ptr<State> state;
};
