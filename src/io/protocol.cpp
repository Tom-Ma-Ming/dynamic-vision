#include "vision/io/Protocol.hpp"

#include <cctype>
#include <stdexcept>

namespace vision::serial {
namespace {

bool integerToken(const std::string& token, int* value) {
  if (token.empty()) return false;
  std::size_t index = 0;
  if (token[0] == '-' || token[0] == '+') {
    if (token.size() == 1 || token[0] == '+') return false;
    index = 1;
  }
  if (token[index] == '0' && token.size() - index > 1) return false;
  for (std::size_t i = index; i < token.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
  }
  try {
    std::size_t used = 0;
    const int parsed = std::stoi(token, &used);
    if (used != token.size()) return false;
    if (value) *value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

EncodedMove encodeMove(int dx, int dy) {
  const auto& profile = developmentProfile();
  if (dx < profile.axis_min || dx > profile.axis_max || dy < profile.axis_min || dy > profile.axis_max) {
    throw std::invalid_argument("Move counts exceed the firmware per-segment range");
  }
  EncodedMove move;
  move.dx = dx;
  move.dy = dy;
  move.stop_command = false;
  move.bytes = "M " + std::to_string(dx) + " " + std::to_string(dy) + "\n";
  if (static_cast<int>(move.bytes.size()) > profile.line_buffer_chars) {
    throw std::invalid_argument("Move line exceeds the firmware line buffer");
  }
  return move;
}

bool isCanonicalMove(const std::string& line, int* dx, int* dy) {
  if (line.size() < 6 || line.back() != '\n') return false;
  if (line.find('\n') != line.size() - 1) return false;
  const std::string body = line.substr(0, line.size() - 1);
  if (body.rfind("M ", 0) != 0) return false;
  const auto split = body.find(' ', 2);
  if (split == std::string::npos || body.find(' ', split + 1) != std::string::npos) return false;
  int x = 0, y = 0;
  if (!integerToken(body.substr(2, split - 2), &x) || !integerToken(body.substr(split + 1), &y)) return false;
  const auto& profile = developmentProfile();
  if (x < profile.axis_min || x > profile.axis_max || y < profile.axis_min || y > profile.axis_max) return false;
  if (line != encodeMove(x, y).bytes) return false;
  if (dx) *dx = x;
  if (dy) *dy = y;
  return true;
}

bool isForbiddenThisRound(const std::string& opcode) {
  static const char* kForbidden[] = {"L", "R", "LD", "LU", "RD", "RU", "W", "CFG", "F", "S", "A",
                                     "STOP", "OK", "PROBE_FIRE", "probe_fire"};
  for (const char* item : kForbidden) {
    if (opcode == item) return true;
  }
  return false;
}

}  // namespace vision::serial
