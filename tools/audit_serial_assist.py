"""Independent audit for the serial-assist logs.

Eligibility, hold, terminals and effect numbers are recomputed from the logs.
summary.passed is not the source of truth. UNKNOWN is never counted as a pass.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

PROTOCOL = "serial-assist-audit-v1"
MOVE = re.compile(r"^M (-?\d+) (-?\d+)\n$")
REQUIRED = [
    "manifest.json", "frames.jsonl", "commands.jsonl", "transport.jsonl",
    "controller.jsonl", "summary.json", "report.md",
]
UNKNOWNS = [
    "flashed_firmware_identity", "flashed_firmware_matches_source", "device_model",
    "com_port", "end_to_end_latency_ms", "device_counts_per_px", "usb_hid_queue_depth",
    "partial_write_recovery", "device_watchdog", "ok_semantics_on_device",
    "device_execution_confirmation", "remote_stop_confirmation", "tensorrt_execution",
]


def load_jsonl(path):
    rows, errors = [], []
    text = path.read_text(encoding="utf-8")
    if text and not text.endswith("\n"):
        errors.append(f"{path.name}: missing trailing newline")
    for number, line in enumerate(text.splitlines(), 1):
        if not line:
            errors.append(f"{path.name}:{number}: blank line")
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            errors.append(f"{path.name}:{number}: corrupt json")
    return rows, errors


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def audit_run(directory, acceptance=None):
    directory = Path(directory)
    failures, unknowns = [], []
    missing = [name for name in REQUIRED if not (directory / name).is_file()]
    if missing:
        return _result(directory, "failed", [f"missing:{name}" for name in missing], UNKNOWNS, {})
    try:
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return _result(directory, "failed", ["corrupt manifest or summary"], UNKNOWNS, {})
    frames, frame_errors = load_jsonl(directory / "frames.jsonl")
    commands, command_errors = load_jsonl(directory / "commands.jsonl")
    transport, transport_errors = load_jsonl(directory / "transport.jsonl")
    controller, controller_errors = load_jsonl(directory / "controller.jsonl")
    failures.extend(frame_errors + command_errors + transport_errors + controller_errors)
    role = manifest.get("role")
    if role == "inventory":
        return _inventory(directory, manifest, summary, frames, commands, transport, controller, failures)
    if role != "control_run":
        failures.append("unknown manifest role")
    _profile(manifest, summary, failures, unknowns)
    if [row.get("type") for row in commands] != [row.get("type") for row in transport] or \
            [row.get("command_id") for row in commands] != [row.get("command_id") for row in transport]:
        failures.append("command and transport ledgers diverged")
    _frames(frames, controller, manifest, failures)
    _commands(commands, manifest, failures)
    metrics = _effect(frames, commands, manifest, summary, acceptance, failures)
    if summary.get("passed") is True or summary.get("unknown_counted_as_pass") is True:
        failures.append("unknown_or_summary_counted_as_pass")
    if summary.get("overall") == "passed" or summary.get("device_execution") == "passed":
        failures.append("summary_claims_pass")
    if manifest.get("log_policy", {}).get("complete") is not True or manifest.get("log_policy", {}).get("dropped_critical", 1):
        failures.append("critical_log_incomplete")
    if manifest.get("device", {}).get("opened") or manifest.get("serial_opened"):
        failures.append("serial_port_opened")
    status = "failed" if failures else "UNKNOWN"
    return _result(directory, status, failures, unknowns, metrics)


def _inventory(directory, manifest, summary, frames, commands, transport, controller, failures):
    _profile(manifest, summary, failures, [])
    if frames or commands or transport or controller:
        failures.append("inventory_sent_or_recorded_motion")
    if summary.get("passed") is True:
        failures.append("unknown_or_summary_counted_as_pass")
    if manifest.get("device", {}).get("opened") or manifest.get("serial_opened"):
        failures.append("serial_port_opened")
    if not isinstance(manifest.get("commit"), str) or len(manifest.get("commit", "")) != 40:
        failures.append("commit_not_frozen")
    if manifest.get("dirty") is not False:
        failures.append("worktree_dirty")
    status = "failed" if failures else "UNKNOWN"
    return _result(directory, status, failures, list(UNKNOWNS), {"role": "inventory"})


def _profile(manifest, summary, failures, unknowns):
    protocol = manifest.get("protocol") or manifest
    if protocol.get("name") != "legacy_m_noack" or protocol.get("baud") != 115200:
        failures.append("profile_mismatch")
    if protocol.get("ack") or protocol.get("sequence") or protocol.get("stop") or protocol.get("execution_feedback"):
        failures.append("profile_capability_not_noack")
    if protocol.get("supported_motion") != "M" or protocol.get("m00_is_stop") is True:
        failures.append("motion_or_stop_mismatch")
    listed = set(manifest.get("unknown_fields") or [])
    if not listed and isinstance(summary.get("unknowns"), list):
        listed = set(summary["unknowns"])
    for field in UNKNOWNS:
        if field not in listed and field not in set(summary.get("unknowns") or []):
            failures.append(f"unknown_field_not_listed:{field}")
        else:
            unknowns.append(field)
    if manifest.get("click_migrated") is True:
        failures.append("click_migrated")


def _frames(frames, controller, manifest, failures):
    if not frames:
        failures.append("missing_frames")
        return
    indexes = [row.get("frame") for row in frames]
    if indexes != list(range(len(frames))):
        failures.append("missing_or_duplicate_frames")
    previous = None
    for row in frames:
        for key in ("control_ns", "capture_ns", "publish_ns", "est_x", "est_y", "truth_x", "truth_y", "error_px"):
            if key.endswith("_ns"):
                if not isinstance(row.get(key), int):
                    failures.append(f"bad_clock:{row.get('frame')}")
            elif not finite(row.get(key)):
                failures.append(f"nonfinite:{row.get('frame')}:{key}")
        if previous is not None and row["control_ns"] <= previous:
            failures.append("clock_regression")
        previous = row["control_ns"]
        if row.get("media_pts_ns") is not None or row.get("gpu_event_ns") is not None or row.get("device_time_ns") is not None:
            failures.append("clocks_were_combined")
        if row.get("truth_use") != "score_only":
            failures.append("truth_not_marked_score_only")
    if len(controller) != len(frames):
        failures.append("controller_frame_count")
        return
    params = manifest.get("parameters") or {}
    center_x = params.get("center_x", frames[0].get("center_x"))
    center_y = params.get("center_y", frames[0].get("center_y"))
    for frame, control in zip(frames, controller):
        if "truth_x" in control or "truth_y" in control or control.get("used_hidden_truth") is True:
            failures.append("hidden_truth_in_controller")
        if not finite(control.get("error_x_px")) or not finite(control.get("error_y_px")):
            failures.append("controller_error_missing")
            continue
        if abs(control["error_x_px"] - (frame["est_x"] - center_x)) > 1e-6:
            failures.append("controller_used_other_than_estimate_x")
        if abs(control["error_y_px"] - (frame["est_y"] - center_y)) > 1e-6:
            failures.append("controller_used_other_than_estimate_y")
        if bool(control.get("emit")) != bool(frame.get("emit")):
            failures.append("emit_mismatch")
        if frame.get("controller") == "off" and frame.get("emit"):
            failures.append("off_emitted")
        if frame.get("controller") == "limited_step" and (control.get("p_x") or control.get("i_x") or control.get("d_x")):
            failures.append("limited_step_recorded_pid_term")
    _gates(frames, manifest, failures)


def _gates(frames, manifest, failures):
    params = manifest.get("parameters") or {}
    enter = params.get("enter_px")
    exit_px = params.get("exit_px")
    turn = params.get("human_turn_px")
    resume_s = params.get("resume_wait_s")
    limit = params.get("max_counts_per_axis")
    firmware = params.get("firmware_axis_limit", 127)
    center_x = params.get("center_x")
    center_y = params.get("center_y")
    max_age = params.get("max_age_ms", 200)
    cov_limit = params.get("covariance_limit", 200)
    if center_x is None:
        center_x = frames[0].get("center_x")
    if center_y is None:
        center_y = frames[0].get("center_y")
    if not all(finite(value) for value in (enter, exit_px, turn, resume_s, center_x, center_y)):
        failures.append("assist_parameters_missing")
        return
    resume_ns = int(round(resume_s * 1e9))
    cap = min(int(limit), int(firmware))
    hold = False
    latched = False
    selected = None
    resume_at = None
    for row in frames:
        now = row["control_ns"]
        if row.get("human_explicit"):
            hold = True
            resume_at = None
            latched = False
        elif row.get("human_release") and hold:
            hold = False
            resume_at = now + resume_ns
            latched = False
        elif not hold and math.hypot(row.get("human_dx_px") or 0, row.get("human_dy_px") or 0) >= turn:
            resume_at = now + resume_ns
            latched = False
        waiting = resume_at is not None and now < resume_at
        ready = row.get("has_track") is True and row.get("track_state") == "Confirmed" and not row.get("ambiguous") \
            and not row.get("identity_conflict") and row.get("track_id") and row.get("age_ms", 0) <= max_age \
            and row.get("covariance", 0) <= cov_limit
        if ready and selected is None:
            selected = row["track_id"]
        if ready and selected is not None and row["track_id"] != selected:
            ready = False
        distance = math.hypot(row["est_x"] - center_x, row["est_y"] - center_y)
        if not ready or hold or waiting:
            latched = False
        elif not latched and distance <= enter:
            latched = True
        elif latched and distance > exit_px:
            latched = False
        if row.get("emit"):
            if hold:
                failures.append("assist_during_hold")
            if waiting or not ready or not latched:
                failures.append("assist_outside_recomputed_gate")
            if abs(int(row.get("assist_dx_counts") or 0)) > cap or abs(int(row.get("assist_dy_counts") or 0)) > cap:
                failures.append("over_limit_counts")
            if selected is not None and row.get("track_id") != selected:
                failures.append("identity_switch_output")
        if manifest.get("run_role") == "boundary" and row.get("emit"):
            failures.append("boundary_produced_assist")


def _commands(commands, manifest, failures):
    ack = (manifest.get("protocol") or {}).get("ack")
    backend = (manifest.get("parameters") or {}).get("backend")
    delay = (manifest.get("parameters") or {}).get("apply_delay_ns", 0)
    grouped = {}
    partial = False
    sent_after_partial = False
    for row in commands:
        if row.get("stop_command") is True or "STOP" in str(row.get("type")):
            failures.append("forged_stop")
        if row.get("authorizes_epoch") is True:
            failures.append("ack_authorized_epoch")
        if row.get("execution_source") == "device_verified":
            failures.append("forged_device_verified")
        if row.get("type") == "ack_received" and not ack:
            failures.append("ack_on_noack_profile")
        intended = row.get("intended") or ""
        if intended:
            match = MOVE.fullmatch(intended)
            if not match:
                failures.append("noncanonical_move")
            else:
                dx, dy = int(match.group(1)), int(match.group(2))
                if abs(dx) > 127 or abs(dy) > 127:
                    failures.append("firmware_range")
                if dx == 0 and dy == 0 and row.get("stop_command") is True:
                    failures.append("m00_used_as_stop")
        if row.get("error") == "partial_write_unresolved":
            partial = True
            accepted = row.get("accepted") or ""
            if "\n" in accepted or row.get("physically_sent") is True:
                failures.append("partial_write_completed_or_newline")
        if partial and row.get("physically_sent") is True and row.get("error") != "partial_write_unresolved":
            sent_after_partial = True
        command_id = row.get("command_id") or 0
        if not command_id:
            if row.get("host_terminal"):
                failures.append("anonymous_terminal")
            continue
        grouped.setdefault(command_id, []).append(row)
    if sent_after_partial:
        failures.append("motion_after_partial_write")
    for command_id, rows in grouped.items():
        terminals = [row for row in rows if row.get("host_terminal")]
        if len(terminals) != 1:
            failures.append(f"terminal_count:{command_id}:{len(terminals)}")
            continue
        terminal = terminals[0]
        if terminal["host_terminal"] == "write_completed":
            if terminal.get("device_result") != "UNKNOWN" or terminal.get("applied_ns") is not None:
                failures.append("write_completed_forged_execution")
            if not terminal.get("physically_sent"):
                failures.append("write_completed_without_bytes")
        observed = [row for row in rows if row.get("type") == "execution_observed"]
        for row in observed:
            if row.get("host_terminal"):
                failures.append("execution_observation_is_terminal")
            if row.get("execution_source") != "simulator" or backend != "simulator":
                failures.append("forged_execution_source")
            if row.get("applied_ns") is None:
                failures.append("simulator_apply_without_time")
            elif row["applied_ns"] < terminal["time_ns"] or (row["applied_ns"] == terminal["time_ns"] and delay):
                failures.append("applied_time_copied_or_early")
        if terminal.get("device_result") == "UNKNOWN" and any(row.get("applied_ns") is not None and row.get("type") != "execution_observed" for row in rows):
            failures.append("applied_ns_on_unknown")


def _effect(frames, commands, manifest, summary, acceptance, failures):
    metrics = {}
    if not frames:
        return metrics
    window = 25
    limit = 2.5
    coverage_min = 0.9
    if acceptance:
        static = acceptance.get("synthetic_static") or {}
        window = int(static.get("end_window_frames", window))
        limit = static.get("end_error_px_max", limit)
        coverage_min = static.get("eligible_coverage_min", coverage_min)
    errors = [row["error_px"] for row in frames]
    end = errors[-min(window, len(errors)):]
    metrics["end_max_truth_error_px"] = max(end) if end else None
    params = manifest.get("parameters") or {}
    demand = assisted = 0
    # Recompute eligibility the same way as _gates, then score coverage from
    # emit, a nonzero limited step, or a command whose lifetime covers the frame.
    enter = params.get("enter_px")
    exit_px = params.get("exit_px")
    deadzone = params.get("deadzone_px")
    turn = params.get("human_turn_px")
    resume_s = params.get("resume_wait_s")
    center_x = params.get("center_x", frames[0].get("center_x"))
    center_y = params.get("center_y", frames[0].get("center_y"))
    if all(finite(value) for value in (enter, exit_px, deadzone, turn, resume_s, center_x, center_y)):
        resume_ns = int(round(resume_s * 1e9))
        hold = False
        latched = False
        selected = None
        resume_at = None
        live = []
        for row in frames:
            now = row["control_ns"]
            if row.get("human_explicit"):
                hold, resume_at, latched = True, None, False
            elif row.get("human_release") and hold:
                hold, latched = False, False
                resume_at = now + resume_ns
            elif not hold and math.hypot(row.get("human_dx_px") or 0, row.get("human_dy_px") or 0) >= turn:
                resume_at, latched = now + resume_ns, False
            waiting = resume_at is not None and now < resume_at
            ready = row.get("has_track") is True and row.get("track_state") == "Confirmed" and row.get("track_id")
            if ready and selected is None:
                selected = row["track_id"]
            if ready and selected is not None and row["track_id"] != selected:
                ready = False
            distance = math.hypot(row["est_x"] - center_x, row["est_y"] - center_y)
            if not ready or hold or waiting:
                latched = False
            elif not latched and distance <= enter:
                latched = True
            elif latched and distance > exit_px:
                latched = False
            if latched and ready and not hold and not waiting and distance > deadzone:
                demand += 1
                covered = row.get("emit") or math.hypot(row.get("assist_dx_px") or 0, row.get("assist_dy_px") or 0) > 1e-6
                if not covered:
                    covered = any(item["generated_ns"] <= now < item["deadline_ns"] for item in live)
                if covered:
                    assisted += 1
            if row.get("emit"):
                live.append({"generated_ns": now, "deadline_ns": now + int(params.get("command_lifetime_ns") or 0)})
    metrics["coverage"] = (assisted / demand) if demand else None
    metrics["demand_frames"] = demand
    role = manifest.get("run_role")
    controller = (manifest.get("parameters") or {}).get("controller")
    delay = (manifest.get("parameters") or {}).get("apply_delay_ns") or 0
    if role == "effect" and controller not in (None, "off") and delay == 0 and finite(limit):
        if metrics["end_max_truth_error_px"] is None or metrics["end_max_truth_error_px"] > limit:
            failures.append("end_error_above_frozen_threshold")
        if metrics["coverage"] is None or metrics["coverage"] < coverage_min:
            failures.append("coverage_below_frozen_threshold")
    elif role in ("delay", "walk") or delay:
        unknowns_note = "effect threshold not applied; latency and moving-target bars are UNKNOWN"
        metrics["effect_threshold"] = unknowns_note
    if controller == "off" and any(row.get("emit") for row in frames):
        failures.append("off_emitted")
    if summary.get("effect_ok") is True and "end_error_above_frozen_threshold" in failures:
        failures.append("summary_effect_disagrees")
    return metrics


def _result(directory, status, failures, unknowns, metrics):
    deduped = []
    for item in failures:
        if item not in deduped:
            deduped.append(item)
    return {
        "protocol": PROTOCOL,
        "directory": str(directory),
        "status": status,
        "passed": False,
        "unknown_counted_as_pass": False,
        "failures": deduped,
        "unknowns": unknowns,
        "metrics": metrics,
    }


def audit_tree(parent, acceptance=None):
    parent = Path(parent)
    runs = []
    for manifest in sorted(parent.rglob("manifest.json")):
        runs.append(audit_run(manifest.parent, acceptance))
    groups = {}
    for run in runs:
        if run["status"] == "failed" and run["metrics"].get("role") == "inventory":
            continue
        directory = Path(run["directory"])
        try:
            summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        key = f"{summary.get('scene')}#{summary.get('seed')}"
        groups.setdefault(key, {})[summary.get("controller")] = {
            "trajectory": summary.get("human_trajectory_sha256"),
            "end": summary.get("end_max_truth_error_px"),
            "role": summary.get("role"),
            "directory": str(directory),
        }
    failures = []
    for key, modes in groups.items():
        hashes = {item["trajectory"] for item in modes.values() if item["trajectory"]}
        if len(hashes) > 1:
            failures.append(f"human_trajectory_mismatch:{key}")
        if {"off", "limited_step", "fixed_pid"} <= set(modes) and modes["limited_step"]["role"] == "effect":
            off = modes["off"]["end"]
            step = modes["limited_step"]["end"]
            pid = modes["fixed_pid"]["end"]
            if not (finite(off) and finite(step) and finite(pid) and off > step + 5 and off > pid + 5):
                failures.append(f"comparison_did_not_improve_on_off:{key}")
            step_rows = [json.loads(line) for line in (Path(modes["limited_step"]["directory"]) / "controller.jsonl").read_text(encoding="utf-8").splitlines() if line]
            pid_rows = [json.loads(line) for line in (Path(modes["fixed_pid"]["directory"]) / "controller.jsonl").read_text(encoding="utf-8").splitlines() if line]
            if max(abs(row.get("p_x") or 0) for row in step_rows) != 0:
                failures.append(f"limited_step_used_pid:{key}")
            if max(abs(row.get("p_x") or 0) for row in pid_rows) == 0:
                failures.append(f"fixed_pid_missing_p_term:{key}")
    status = "failed" if failures or any(run["status"] == "failed" for run in runs) else "UNKNOWN"
    return {
        "protocol": PROTOCOL,
        "status": status,
        "passed": False,
        "unknown_counted_as_pass": False,
        "failures": failures,
        "runs": runs,
        "unknowns": UNKNOWNS,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--tree", action="store_true")
    parser.add_argument("--acceptance", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()
    acceptance = json.loads(args.acceptance.read_text(encoding="utf-8")) if args.acceptance else None
    report = audit_tree(args.directory, acceptance) if args.tree else audit_run(args.directory, acceptance)
    destination = args.out or (args.directory / ("audit-summary.json" if args.tree else "audit.json"))
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": len(report["failures"]), "passed": False}))
    return 1 if report["status"] == "failed" else 2


if __name__ == "__main__":
    sys.exit(main())
