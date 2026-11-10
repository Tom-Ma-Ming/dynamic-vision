#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vision::serial {

// Development profile from the firmware source baseline. This is not an
// identification of the firmware currently flashed on a device.
struct SerialProfile {
  std::string name = "legacy_m_noack";
  int baud = 115200;
  int data_bits = 8;
  std::string parity = "none";
  int stop_bits = 1;
  std::string host_serial = "8N1";
  bool ack = false;
  bool sequence = false;
  bool stop = false;
  bool execution_feedback = false;
  bool watchdog = false;
  std::string supported_motion = "M";
  int axis_min = -127;
  int axis_max = 127;
  int line_buffer_chars = 40;
  // Firmware delay between Mouse.move segments. Not an end-to-end latency.
  int segment_delay_ms = 2;
};

struct SourceBaseline {
  std::string firmware_path = "E:/mac-share/mouse_firmware/mouse_firmware.ino";
  std::string firmware_sha256 = "8F5D59E13C0F3C0165F776A6034F2283066A7A30D76B78746B0C3BDB90AF25B3";
  std::string host_path =
      "E:/mac-share/tensorRT_Pro-main/tensorRT_Pro-main/src/application/auto_aim/serial_mouse.cpp";
  std::string host_sha256 = "44EDDB7763111C9D154D6FAED6E15B5940820C6E5A13880E64478C85C6E7EDFB";
  std::string note =
      "Source hashes do not prove the firmware currently flashed on a device.";
};

inline const SerialProfile& developmentProfile() {
  static const SerialProfile profile;
  return profile;
}

inline const SourceBaseline& sourceBaseline() {
  static const SourceBaseline baseline;
  return baseline;
}

// Fields the source baseline does not establish. Callers must keep these
// UNKNOWN and must not count them as a pass.
inline const std::vector<std::string>& unknownFields() {
  static const std::vector<std::string> fields = {
      "flashed_firmware_identity",
      "flashed_firmware_matches_source",
      "device_model",
      "com_port",
      "end_to_end_latency_ms",
      "device_counts_per_px",
      "usb_hid_queue_depth",
      "partial_write_recovery",
      "device_watchdog",
      "ok_semantics_on_device",
      "device_execution_confirmation",
      "remote_stop_confirmation",
      "tensorrt_execution",
  };
  return fields;
}

struct EncodedMove {
  std::string bytes;
  int dx = 0;
  int dy = 0;
  // M 0 0 is a zero move. The firmware has no STOP, and this flag stays false.
  bool stop_command = false;
};

// Canonical ASCII "M <dx> <dy>\n". Rejects values outside the firmware
// per-segment range and lines that would exceed the 40-character buffer.
EncodedMove encodeMove(int dx, int dy);

bool isCanonicalMove(const std::string& line, int* dx = nullptr, int* dy = nullptr);

// Click, fire, wait, CFG and STOP are not sent in this round.
bool isForbiddenThisRound(const std::string& opcode);

}  // namespace vision::serial
