#include "vision/io/Backend.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vision::serial {

const char* backendName(BackendKind kind) {
  switch (kind) {
    case BackendKind::Simulator: return "simulator";
    case BackendKind::MockSerial: return "mock_serial";
    case BackendKind::DryRun: return "dry_run";
    case BackendKind::Serial: return "serial";
  }
  return "unknown";
}

BackendKind parseBackend(const std::string& name) {
  if (name == "simulator") return BackendKind::Simulator;
  if (name == "mock_serial") return BackendKind::MockSerial;
  if (name == "dry_run") return BackendKind::DryRun;
  if (name == "serial") return BackendKind::Serial;
  throw std::invalid_argument("Unknown execution backend: " + name);
}

const char* hostName(HostTerminal terminal) {
  switch (terminal) {
    case HostTerminal::None: return "";
    case HostTerminal::WriteCompleted: return "write_completed";
    case HostTerminal::LocalCancelled: return "local_cancelled";
    case HostTerminal::Expired: return "expired";
    case HostTerminal::Failed: return "failed";
    case HostTerminal::Rejected: return "rejected";
    case HostTerminal::Disconnected: return "disconnected";
    case HostTerminal::DryRunRecorded: return "dry_run_recorded";
  }
  return "";
}

const char* deviceName(DeviceResult result) {
  switch (result) {
    case DeviceResult::Unknown: return "UNKNOWN";
    case DeviceResult::NotSent: return "not_sent";
    case DeviceResult::NotSupported: return "not_supported";
    case DeviceResult::SimulatorApplied: return "simulator_applied";
  }
  return "UNKNOWN";
}

ExecutionBackend::ExecutionBackend(BackendConfig config) : config_(std::move(config)) {
  if (config_.profile.name != "legacy_m_noack" && config_.ack_enabled && config_.profile.ack) {
    // Optional ack profiles are explicit. The development default never sets ack.
  }
  if (!config_.profile.ack) config_.ack_enabled = false;
  if (config_.kind != BackendKind::Serial) refusal_.clear();
}

Event ExecutionBackend::terminal(const Intent& intent, std::int64_t now_ns, const char* type,
                                HostTerminal host, DeviceResult device, const std::string& error) const {
  Event event;
  event.type = type;
  event.intent = intent;
  event.time_ns = now_ns;
  event.host = host;
  event.device = device;
  event.error = error;
  event.stop_command = false;
  event.authorizes_epoch = false;
  event.has_applied_ns = false;
  return event;
}

std::vector<Event> ExecutionBackend::requestOpen(const std::string& port, std::int64_t now_ns) {
  // No port scan and no OS open. SA05 owns a real Windows transport.
  if (port.empty() || port == "auto" || port == "scan" || port == "AUTO") {
    refusal_ = "auto_scan_refused";
  } else {
    refusal_ = "real_port_not_opened";
  }
  Event event;
  event.type = "open_refused";
  event.time_ns = now_ns;
  event.error = refusal_;
  event.host = HostTerminal::None;
  event.device = DeviceResult::NotSent;
  event.raw_response = port;
  return {event};
}

std::vector<Event> ExecutionBackend::failPending(std::int64_t now_ns, const std::string& error) {
  std::vector<Event> events;
  if (!have_pending_) return events;
  events.push_back(terminal(pending_, now_ns, "failed", HostTerminal::Failed, DeviceResult::NotSent, error));
  have_pending_ = false;
  return events;
}

std::vector<Event> ExecutionBackend::submit(const Intent& intent, std::int64_t now_ns) {
  std::vector<Event> events;
  if (!intent.command_id ||
      std::find(seen_ids_.begin(), seen_ids_.end(), intent.command_id) != seen_ids_.end()) {
    events.push_back(terminal(intent, now_ns, "rejected", HostTerminal::Rejected, DeviceResult::NotSent,
                              "duplicate_or_empty_command_id"));
    return events;
  }
  seen_ids_.push_back(intent.command_id);
  if (intent.dx_counts < developmentProfile().axis_min || intent.dx_counts > developmentProfile().axis_max ||
      intent.dy_counts < developmentProfile().axis_min || intent.dy_counts > developmentProfile().axis_max) {
    events.push_back(terminal(intent, now_ns, "rejected", HostTerminal::Rejected, DeviceResult::NotSent,
                              "counts_outside_firmware_segment"));
    return events;
  }
  if (config_.kind == BackendKind::Serial) {
    auto event = terminal(intent, now_ns, "disconnected", HostTerminal::Disconnected, DeviceResult::NotSent,
                          refusal_.empty() ? "serial_closed" : refusal_);
    events.push_back(event);
    return events;
  }
  if (hold_) {
    events.push_back(terminal(intent, now_ns, "rejected", HostTerminal::Rejected, DeviceResult::NotSent, "human_hold"));
    return events;
  }
  if (faulted_) {
    events.push_back(terminal(intent, now_ns, "failed", HostTerminal::Failed, DeviceResult::NotSent, "transport_fault"));
    return events;
  }
  if (have_pending_) {
    auto cancelled = terminal(pending_, now_ns, "local_cancelled", HostTerminal::LocalCancelled,
                              DeviceResult::NotSent, "superseded_by_newer");
    events.push_back(cancelled);
    have_pending_ = false;
  }
  pending_ = intent;
  have_pending_ = true;
  Event queued = terminal(intent, now_ns, "queued", HostTerminal::None, DeviceResult::Unknown, "");
  events.push_back(queued);
  return events;
}

std::vector<Event> ExecutionBackend::takeover(std::int64_t now_ns, bool latch_hold) {
  if (latch_hold) hold_ = true;
  std::vector<Event> events;
  Event requested;
  requested.type = "cancel_requested";
  requested.time_ns = now_ns;
  requested.error = "human_hold";
  requested.device = DeviceResult::NotSupported;
  requested.raw_response = "stop_not_supported";
  events.push_back(requested);
  if (have_pending_) {
    events.push_back(terminal(pending_, now_ns, "local_cancelled", HostTerminal::LocalCancelled,
                              DeviceResult::NotSent, "takeover"));
    have_pending_ = false;
  }
  return events;
}

std::vector<Event> ExecutionBackend::release(std::int64_t now_ns) {
  hold_ = false;
  Event event;
  event.type = "hold_released";
  event.time_ns = now_ns;
  event.error = "release_does_not_send_motion";
  return {event};
}

std::vector<Event> ExecutionBackend::tick(std::int64_t now_ns) {
  std::vector<Event> events;
  if (have_pending_ && pending_.deadline_ns <= now_ns) {
    events.push_back(terminal(pending_, now_ns, "expired", HostTerminal::Expired, DeviceResult::NotSent,
                              "deadline_before_write"));
    have_pending_ = false;
  }
  if (config_.kind != BackendKind::Simulator) return events;
  for (auto& item : scheduled_) {
    if (item.done || item.apply_ns > now_ns) continue;
    item.done = true;
    Event observed;
    observed.type = "execution_observed";
    observed.intent = item.intent;
    observed.time_ns = now_ns;
    observed.host = HostTerminal::None;
    observed.device = DeviceResult::SimulatorApplied;
    observed.has_applied_ns = true;
    observed.applied_ns = item.apply_ns;
    observed.execution_source = "simulator";
    observed.physically_sent = true;
    observed.error = item.apply_ns == item.write_ns ? "simulator_delay_zero" : "";
    events.push_back(observed);
    applications_.push_back({item.intent, item.apply_ns, item.intent.dx_counts, item.intent.dy_counts});
  }
  return events;
}

std::vector<Event> ExecutionBackend::pump(std::int64_t now_ns) {
  std::vector<Event> events;
  if (!have_pending_) return events;
  if (faulted_) return failPending(now_ns, "transport_fault");
  if (pending_.deadline_ns <= now_ns) {
    events.push_back(terminal(pending_, now_ns, "expired", HostTerminal::Expired, DeviceResult::NotSent,
                              "deadline_before_write"));
    have_pending_ = false;
    return events;
  }
  if (have_sent_ && now_ns < next_send_ns_) return events;
  Intent intent = pending_;
  have_pending_ = false;
  EncodedMove move;
  try {
    move = encodeMove(intent.dx_counts, intent.dy_counts);
  } catch (const std::invalid_argument& error) {
    events.push_back(terminal(intent, now_ns, "rejected", HostTerminal::Rejected, DeviceResult::NotSent, error.what()));
    return events;
  }
  Event started = terminal(intent, now_ns, "write_started", HostTerminal::None, DeviceResult::Unknown, "");
  started.intended = move.bytes;
  events.push_back(started);

  if (config_.fail_all_writes) {
    faulted_ = true;
    auto failed = terminal(intent, now_ns, "failed", HostTerminal::Failed, DeviceResult::NotSent, "write_failed");
    failed.intended = move.bytes;
    failed.bytes_accepted = 0;
    events.push_back(failed);
    have_sent_ = true;
    next_send_ns_ = now_ns + config_.min_send_interval_ns;
    return events;
  }
  if (config_.kind == BackendKind::DryRun) {
    auto recorded = terminal(intent, now_ns, "dry_run_recorded", HostTerminal::DryRunRecorded,
                             DeviceResult::NotSent, "not_sent");
    recorded.intended = move.bytes;
    recorded.accepted = move.bytes;
    recorded.bytes_accepted = static_cast<int>(move.bytes.size());
    recorded.physically_sent = false;
    events.push_back(recorded);
    have_sent_ = true;
    next_send_ns_ = now_ns + config_.min_send_interval_ns;
    return events;
  }
  if (config_.partial_byte_limit >= 0 &&
      config_.partial_byte_limit < static_cast<int>(move.bytes.size())) {
    const std::string prefix = move.bytes.substr(0, static_cast<std::size_t>(config_.partial_byte_limit));
    device_buffer_ += prefix;
    faulted_ = true;
    auto failed = terminal(intent, now_ns, "failed", HostTerminal::Failed, DeviceResult::Unknown,
                           "partial_write_unresolved");
    failed.intended = move.bytes;
    failed.accepted = prefix;
    failed.bytes_accepted = static_cast<int>(prefix.size());
    failed.physically_sent = false;
    events.push_back(failed);
    return events;
  }

  device_buffer_ += move.bytes;
  auto completed = terminal(intent, now_ns, "write_completed", HostTerminal::WriteCompleted,
                            DeviceResult::Unknown, "");
  completed.intended = move.bytes;
  completed.accepted = move.bytes;
  completed.bytes_accepted = static_cast<int>(move.bytes.size());
  completed.physically_sent = true;
  completed.has_applied_ns = false;
  events.push_back(completed);
  have_sent_ = true;
  next_send_ns_ = now_ns + config_.min_send_interval_ns;
  if (config_.kind == BackendKind::Simulator) {
    scheduled_.push_back({intent, now_ns + config_.apply_delay_ns, now_ns, false});
  }
  // noack: do not wait for OK and do not invent an ACK.
  return events;
}

std::vector<Event> ExecutionBackend::pushAck(std::uint64_t ack_epoch, std::int64_t now_ns, const std::string& raw) {
  Event event;
  event.time_ns = now_ns;
  event.raw_response = raw;
  event.authorizes_epoch = false;
  event.has_applied_ns = false;
  event.stop_command = false;
  if (!config_.ack_enabled || !config_.profile.ack) {
    event.type = "ack_ignored";
    event.match = "none";
    event.error = "ack_not_in_profile";
    event.device = DeviceResult::NotSupported;
    return {event};
  }
  event.type = "ack_received";
  event.match = "inferred";
  event.device = DeviceResult::Unknown;
  event.intent.epoch = ack_epoch;
  if (ack_epoch != epoch_) {
    event.error = "stale_epoch";
  } else {
    event.error = "ack_is_not_execution";
  }
  return {event};
}

std::vector<AppliedMotion> ExecutionBackend::takeApplications() {
  std::vector<AppliedMotion> out;
  out.swap(applications_);
  return out;
}

}  // namespace vision::serial
