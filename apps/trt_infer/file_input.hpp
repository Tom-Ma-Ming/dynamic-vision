#pragma once
#include <fstream>
#include <stdexcept>
#include <vector>
inline std::vector<char> readFile(const char* path, size_t expected = 0) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto length = file.tellg();
    if (length <= 0 || (expected && static_cast<size_t>(length) != expected))
        throw std::runtime_error(std::string("Missing, empty or wrong size: ") + path);
    std::vector<char> data(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(data.data(), length)) throw std::runtime_error("Read failed");
    return data;
}

