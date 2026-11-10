#include "vision/io/Sha256.hpp"

#include <iomanip>

namespace vision::serial {
namespace {

constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

void Sha256::reset() {
  datalen_ = 0;
  bitlen_ = 0;
  state_[0] = 0x6a09e667;
  state_[1] = 0xbb67ae85;
  state_[2] = 0x3c6ef372;
  state_[3] = 0xa54ff53a;
  state_[4] = 0x510e527f;
  state_[5] = 0x9b05688c;
  state_[6] = 0x1f83d9ab;
  state_[7] = 0x5be0cd19;
}

void Sha256::transform() {
  std::uint32_t m[64];
  for (int i = 0; i < 16; ++i) {
    m[i] = (std::uint32_t(data_[i * 4]) << 24) | (std::uint32_t(data_[i * 4 + 1]) << 16) |
           (std::uint32_t(data_[i * 4 + 2]) << 8) | std::uint32_t(data_[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    const std::uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + S1 + ch + k[i] + m[i];
    const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    data_[datalen_++] = data[i];
    if (datalen_ == 64) {
      transform();
      bitlen_ += 512;
      datalen_ = 0;
    }
  }
}

std::string Sha256::hex() {
  std::uint8_t block[64];
  std::uint32_t i = datalen_;
  if (datalen_ < 56) {
    data_[i++] = 0x80;
    while (i < 56) data_[i++] = 0;
  } else {
    data_[i++] = 0x80;
    while (i < 64) data_[i++] = 0;
    transform();
    for (i = 0; i < 56; ++i) data_[i] = 0;
  }
  bitlen_ += std::uint64_t(datalen_) * 8;
  data_[63] = static_cast<std::uint8_t>(bitlen_);
  data_[62] = static_cast<std::uint8_t>(bitlen_ >> 8);
  data_[61] = static_cast<std::uint8_t>(bitlen_ >> 16);
  data_[60] = static_cast<std::uint8_t>(bitlen_ >> 24);
  data_[59] = static_cast<std::uint8_t>(bitlen_ >> 32);
  data_[58] = static_cast<std::uint8_t>(bitlen_ >> 40);
  data_[57] = static_cast<std::uint8_t>(bitlen_ >> 48);
  data_[56] = static_cast<std::uint8_t>(bitlen_ >> 56);
  transform();
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int word = 0; word < 8; ++word) {
    out << std::setw(8) << state_[word];
  }
  reset();
  return out.str();
}

}  // namespace vision::serial
