import importlib.util
import json
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location("serial_audit", Path(__file__).parents[2] / "tools/audit_serial_assist.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def frame(index, emit=False, counts=0, hold=False):
    return {
        "frame": index, "control_ns": index * 40000000, "capture_ns": index * 40000000,
        "publish_ns": index * 40000000, "media_pts_ns": None, "gpu_event_ns": None,
        "device_time_ns": None, "est_x": 340.0, "est_y": 240.0, "truth_x": 340.0, "truth_y": 240.0,
        "error_px": 20.0, "truth_use": "score_only", "has_track": True, "track_state": "Confirmed",
        "ambiguous": False, "identity_conflict": False, "track_id": 1, "age_ms": 0, "covariance": 0.1,
        "human_explicit": hold and index == 1, "human_release": False, "human_dx_px": 0, "human_dy_px": 0,
        "emit": emit, "assist_dx_counts": counts, "assist_dy_counts": 0, "assist_dx_px": counts,
        "assist_dy_px": 0, "controller": "limited_step",
    }


def control(index, emit=False):
    return {
        "frame": index, "error_x_px": 20.0, "error_y_px": 0.0, "emit": emit, "used_hidden_truth": False,
        "p_x": 0, "i_x": 0, "d_x": 0,
    }


def manifest():
    return {
        "role": "control_run", "run_role": "contract", "commit": "a" * 40, "dirty": False,
        "click_migrated": False, "serial_opened": False, "device": {"opened": False},
        "protocol": {"name": "legacy_m_noack", "baud": 115200, "ack": False, "sequence": False,
                     "stop": False, "execution_feedback": False, "supported_motion": "M", "m00_is_stop": False},
        "unknown_fields": list(module.UNKNOWNS),
        "parameters": {"controller": "limited_step", "backend": "mock_serial", "enter_px": 48, "exit_px": 72,
                       "deadzone_px": 1, "human_turn_px": 12, "resume_wait_s": 0.2, "center_x": 320,
                       "center_y": 240, "max_counts_per_axis": 2, "firmware_axis_limit": 127,
                       "max_age_ms": 200, "covariance_limit": 200, "apply_delay_ns": 0,
                       "command_lifetime_ns": 80000000},
        "log_policy": {"complete": True, "dropped_critical": 0},
    }


def summary():
    return {"passed": False, "unknown_counted_as_pass": False, "overall": "UNKNOWN",
            "device_execution": "UNKNOWN", "tensorrt": "UNKNOWN", "unknowns": list(module.UNKNOWNS),
            "effect_ok": False}


def write_run(path, frames, commands, controls=None, summary_body=None, manifest_body=None):
    path.mkdir(parents=True, exist_ok=True)
    (path / "manifest.json").write_text(json.dumps(manifest_body or manifest()), encoding="utf-8")
    (path / "summary.json").write_text(json.dumps(summary_body or summary()), encoding="utf-8")
    (path / "report.md").write_text("report\n", encoding="utf-8")
    (path / "frames.jsonl").write_text("".join(json.dumps(row) + "\n" for row in frames), encoding="utf-8")
    (path / "commands.jsonl").write_text("".join(json.dumps(row) + "\n" for row in commands), encoding="utf-8")
    (path / "transport.jsonl").write_text("".join(json.dumps(row) + "\n" for row in commands), encoding="utf-8")
    (path / "controller.jsonl").write_text(
        "".join(json.dumps(row) + "\n" for row in (controls or [control(i) for i in range(len(frames))])),
        encoding="utf-8")


def test_clean_contract_is_unknown_not_pass(tmp_path):
    rows = [frame(0), frame(1)]
    report = _audit(tmp_path, rows, [])
    assert report["status"] == "UNKNOWN"
    assert report["passed"] is False
    assert report["unknown_counted_as_pass"] is False


def test_corrupt_line_is_rejected(tmp_path):
    write_run(tmp_path, [frame(0)], [])
    with (tmp_path / "commands.jsonl").open("a", encoding="utf-8") as handle:
        handle.write("{not json}\n")
    report = module.audit_run(tmp_path)
    assert report["status"] == "failed"
    assert any("corrupt" in item for item in report["failures"])


def test_missing_frame_is_rejected(tmp_path):
    report = _audit(tmp_path, [frame(0), frame(2)], [])
    assert report["status"] == "failed"
    assert "missing_or_duplicate_frames" in report["failures"]


def test_duplicate_terminal_is_rejected(tmp_path):
    command = {"command_id": 1, "type": "write_completed", "host_terminal": "write_completed",
               "device_result": "UNKNOWN", "applied_ns": None, "physically_sent": True,
               "intended": "M 1 0\n", "stop_command": False, "authorizes_epoch": False,
               "execution_source": "none", "time_ns": 1, "error": ""}
    report = _audit(tmp_path, [frame(0)], [command, dict(command)])
    assert report["status"] == "failed"
    assert any(item.startswith("terminal_count") for item in report["failures"])


def test_fake_success_is_rejected(tmp_path):
    body = summary()
    body["passed"] = True
    body["overall"] = "passed"
    write_run(tmp_path, [frame(0)], [], summary_body=body)
    command = {"command_id": 1, "type": "write_completed", "host_terminal": "write_completed",
               "device_result": "applied", "applied_ns": 5, "physically_sent": True,
               "intended": "M 1 0\n", "stop_command": False, "authorizes_epoch": False,
               "execution_source": "device_verified", "time_ns": 5, "error": ""}
    (tmp_path / "commands.jsonl").write_text(json.dumps(command) + "\n", encoding="utf-8")
    (tmp_path / "transport.jsonl").write_text(json.dumps(command) + "\n", encoding="utf-8")
    report = module.audit_run(tmp_path)
    assert report["status"] == "failed"
    assert "unknown_or_summary_counted_as_pass" in report["failures"]
    assert "forged_device_verified" in report["failures"]
    assert "write_completed_forged_execution" in report["failures"]


def test_hold_output_is_rejected(tmp_path):
    rows = [frame(0), frame(1, emit=True, counts=1, hold=True)]
    controls = [control(0), control(1, emit=True)]
    report = _audit(tmp_path, rows, [], controls)
    assert report["status"] == "failed"
    assert "assist_during_hold" in report["failures"]


def _audit(path, frames, commands, controls=None):
    write_run(path, frames, commands, controls)
    return module.audit_run(path)
