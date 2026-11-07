#include "detector.hpp"
#include "file_input.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) try {
    if (argc != 4) throw std::runtime_error("usage: trt_infer engine input.f32 output.f32");
    Detector detector(argv[1]);
    auto input = readFile(argv[2], Detector::kInputFloats * sizeof(float));
    std::memcpy(detector.input(), input.data(), input.size());
    const float* output = detector.infer();
    std::ofstream file(argv[3], std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.write(reinterpret_cast<const char*>(output),
               Detector::kOutputFloats * sizeof(float));
    file.close();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}
