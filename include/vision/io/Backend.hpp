#pragma once

#include "vision/io/Protocol.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vision::serial {

enum class BackendKind { Simulator, MockSerial, DryRun, Serial };

const char* backendName(BackendKind kind);
BackendKind parseBackend(const std::string& name);

struct Intent {
  std::string run_id;
  std::uint64_t epoch = 1;
  std::uint64_t command_id = 0;
  std::uint64_t frame_id = 0;
  std::uint64_t track_id = 0;
  std::int64_t generated_ns = 0;
  std::int64_t deadline_ns = 0;
  double error_x_px = 0;
  double error_y_px = 0;
  int dx_counts = 0;
  int dy_counts = 0;
  std::string controller;
  std::string controller_version;
};

enum class HostTerminal {
  None,
  WriteCompleted,
  LocalCancelled,
  Expired,
  Failed,
  Rejected,
  Disconnected,
  DryRunRecorded
};

enum class DeviceResult { Unknown, NotSent, NotSupported, SimulatorApplied };

const char* hostName(HostTerminal terminal);
const char* deviceName(DeviceResult result);

struct Event {
  std::string type;
  Intent intent;
  std::int64_t time_ns = 0;
  int bytes_accepted = 0;
  std::string error;
  std::string raw_response;
  std::string match = "none";
  HostTerminal host = HostTerminal::None;
  DeviceResult device = DeviceResult::Unknown;
  bool has_applied_ns = false;
  std::int64_t applied_ns = 0;
  std::string execution_source = "none";
  std::string intended;
  std::string accepted;
  bool physically_sent = false;
  bool stop_command = false;
  bool authorizes_epoch = false;
};

struct AppliedMotion {
  Intent intent;
  std::int64_t applied_ns = 0;
  int dx_counts = 0;
  int dy_counts = 0;
};

struct BackendConfig {
  BackendKind kind = BackendKind::Simulator;
  SerialProfile profile{};
  std::int64_t min_send_interval_ns = 0;
  std::int64_t apply_delay_ns = 0;
  int partial_byte_limit = -1;
  bool fail_all_writes = false;
  // Optional ack branch. The development profile keeps this false.
  bool ack_enabled = false;
};

// Single owner of the unwritten queue. Does not open a real serial port and
// does not scan ports. Write completion is a host terminal; it does not fill
// applied_ns. Simulator application is a later observation, not a device ACK.
class ExecutionBackend {
 public:
  explicit ExecutionBackend(BackendConfig config);
  BackendKind kind() const { return config_.kind; }
  const BackendConfig& config() const { return config_; }
  bool opened() const { return false; }
  bool faulted() const { return faulted_; }
  bool hold() const { return hold_; }
  bool armed() const { return false; }
  const std::string& deviceBuffer() const { return device_buffer_; }
  const std::string& refusal() const { return refusal_; }
  std::uint64_t epoch() const { return epoch_; }
  void setEpoch(std::uint64_t epoch) { epoch_ = epoch; }

  std::vector<Event> requestOpen(const std::string& port, std::int64_t now_ns);
  std::vector<Event> submit(const Intent& intent, std::int64_t now_ns);
  std::vector<Event> takeover(std::int64_t now_ns, bool latch_hold = true);
  std::vector<Event> release(std::int64_t now_ns);
  std::vector<Event> tick(std::int64_t now_ns);
  std::vector<Event> pump(std::int64_t now_ns);
  std::vector<Event> pushAck(std::uint64_t ack_epoch, std::int64_t now_ns, const std::string& raw);
  std::vector<AppliedMotion> takeApplications();

 private:
  Event terminal(const Intent& intent, std::int64_t now_ns, const char* type, HostTerminal host,
                 DeviceResult device, const std::string& error) const;
  std::vector<Event> failPending(std::int64_t now_ns, const std::string& error);

  BackendConfig config_;
  bool faulted_ = false;
  bool hold_ = false;
  bool have_pending_ = false;
  Intent pending_{};
  std::int64_t next_send_ns_ = 0;
  bool have_sent_ = false;
  std::string device_buffer_;
  std::string refusal_ = "serial_closed";
  std::uint64_t epoch_ = 1;
  std::vector<std::uint64_t> seen_ids_;
  struct Scheduled {
    Intent intent;
    std::int64_t apply_ns = 0;
    std::int64_t write_ns = 0;
    bool done = false;
  };
  std::vector<Scheduled> scheduled_;
  std::vector<AppliedMotion> applications_;
};

}  // namespace vision::serial
