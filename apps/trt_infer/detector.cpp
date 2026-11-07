#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace nvinfer1;

class Logger final : public ILogger {
    void log(Severity level, const char* message) noexcept override {
        if (level <= Severity::kWARNING) std::cerr << message << '\n';
    }
};

void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

struct Buffer {
    void* ptr{};
    explicit Buffer(size_t bytes) { check(cudaMalloc(&ptr, bytes)); }
    ~Buffer() { cudaFree(ptr); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

struct Stream {
    cudaStream_t handle{};
    Stream() { check(cudaStreamCreate(&handle)); }
    ~Stream() { cudaStreamDestroy(handle); }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
};

#include "file_input.hpp"
void requireTensor(const ICudaEngine& engine, const char* name, TensorIOMode mode,
                   std::initializer_list<int> dims) {
    auto shape = engine.getTensorShape(name);
    if (engine.getTensorIOMode(name) != mode || engine.getTensorDataType(name) != DataType::kFLOAT ||
        engine.getTensorFormat(name) != TensorFormat::kLINEAR ||
        shape.nbDims != static_cast<int>(dims.size()) || !std::equal(dims.begin(), dims.end(), shape.d))
        throw std::runtime_error(std::string("Unsupported tensor contract: ") + name);
}

#include "detector.hpp"
struct Detector::State {
    Logger logger;
    std::unique_ptr<IRuntime> runtime;
    std::unique_ptr<ICudaEngine> engine;
    std::unique_ptr<IExecutionContext> ctx;
    Buffer in{3*320*320*sizeof(float)}, out{6*2100*sizeof(float)};
    Stream stream;
    explicit State(const char* enginePath) {
        if (getInferLibVersion() != NV_TENSORRT_VERSION) throw std::runtime_error("TensorRT DLL mismatch");
        auto plan = readFile(enginePath);
        runtime.reset(createInferRuntime(logger));
        if (!runtime) throw std::runtime_error("Cannot create runtime");
        engine.reset(runtime->deserializeCudaEngine(plan.data(), plan.size()));
        if (!engine) throw std::runtime_error("Cannot load engine; rebuild with local GPU and SDK");
        if (engine->getNbIOTensors() != 2) throw std::runtime_error("Expected one input and one output");
        requireTensor(*engine, "images", TensorIOMode::kINPUT, {1,3,320,320});
        requireTensor(*engine, "output0", TensorIOMode::kOUTPUT, {1,6,2100});
        ctx.reset(engine->createExecutionContext());
        if (!ctx) throw std::runtime_error("Cannot create execution context");
        if (!ctx->setTensorAddress("images", in.ptr) || !ctx->setTensorAddress("output0", out.ptr))
            throw std::runtime_error("Cannot bind device buffers");
    }
};
Detector::Detector(const char* path) : state(std::make_unique<State>(path)) {}
Detector::~Detector() = default;
std::vector<float> Detector::infer(const void* input) {
    auto& s = *state;
    std::vector<float> output(6*2100);
    check(cudaMemcpyAsync(s.in.ptr, input, 3*320*320*sizeof(float), cudaMemcpyHostToDevice, s.stream.handle));
    if (!s.ctx->enqueueV3(s.stream.handle)) throw std::runtime_error("enqueueV3 failed");
    check(cudaMemcpyAsync(output.data(), s.out.ptr, output.size()*sizeof(float), cudaMemcpyDeviceToHost, s.stream.handle));
    check(cudaStreamSynchronize(s.stream.handle));
    return output;
}
