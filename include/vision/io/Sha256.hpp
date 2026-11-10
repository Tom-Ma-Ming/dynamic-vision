#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vision::serial {

class Sha256 {
 public:
  Sha256() { reset(); }
  void update(const std::uint8_t* data, std::size_t len);
  void update(const std::string& text) {
    update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  }
  std::string hex();

 private:
  void reset();
  void transform();
  std::uint8_t data_[64]{};
  std::uint32_t datalen_ = 0;
  std::uint64_t bitlen_ = 0;
  std::uint32_t state_[8]{};
};

inline std::string sha256Hex(const std::string& text) {
  Sha256 hasher;
  hasher.update(text);
  return hasher.hex();
}

inline std::string sha256File(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot hash " + path.string());
  Sha256 hasher;
  std::vector<char> buffer(1 << 16);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = in.gcount();
    if (count > 0) hasher.update(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                                  static_cast<std::size_t>(count));
  }
  return hasher.hex();
}

}  // namespace vision::serial
