#include <iostream>
#include <string_view>

#ifdef VISION_PROBE_TRT
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <windows.h>
#include <memory>

bool checked(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return true;
    std::cerr << operation << ": " << cudaGetErrorString(status) << '\n';
    return false;
}
#endif

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "env_probe 1\n";
        return 0;
    }
#ifndef VISION_PROBE_TRT
    std::cerr << "TensorRT support disabled. Configure with windows-sdk and TensorRT_ROOT.\n";
    return 2;
#else
    std::unique_ptr<HINSTANCE__, decltype(&FreeLibrary)> dll(
        LoadLibraryExW(TRT_DLL_PATH, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH), FreeLibrary);
    if (!dll) {
        std::cerr << "Cannot load TensorRT DLL or its dependencies, Windows error " << GetLastError() << '\n';
        return 2;
    }
    auto version = reinterpret_cast<decltype(&getInferLibVersion)>(
        GetProcAddress(dll.get(), "getInferLibVersion"));
    if (!version) {
        std::cerr << "Missing getInferLibVersion in runtime DLL\n";
        return 2;
    }
    int runtime = version();
    std::cout << "TensorRT headers=" << NV_TENSORRT_VERSION << " runtime=" << runtime << '\n';
    if (runtime != NV_TENSORRT_VERSION) {
        std::cerr << "Select matching SDK headers and DLL.\n";
        return 2;
    }
    int driver{}, cuda{}, count{};
    if (!checked(cudaDriverGetVersion(&driver), "cudaDriverGetVersion") ||
        !checked(cudaRuntimeGetVersion(&cuda), "cudaRuntimeGetVersion") ||
        !checked(cudaGetDeviceCount(&count), "cudaGetDeviceCount")) return 3;
    std::cout << "CUDA driver=" << driver << " runtime=" << cuda << " devices=" << count << '\n';
    for (int i = 0; i < count; ++i) {
        cudaDeviceProp props{};
        if (!checked(cudaGetDeviceProperties(&props, i), "cudaGetDeviceProperties")) return 3;
        std::cout << props.name << " SM " << props.major << '.' << props.minor
                  << " memoryMiB=" << props.totalGlobalMem / (1024 * 1024) << '\n';
    }
    return count > 0 ? 0 : 3;
#endif
}
