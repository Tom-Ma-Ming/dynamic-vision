import importlib.util
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "benchmark_replay.py"
SPEC = importlib.util.spec_from_file_location("benchmark_replay", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def test_summarize_uses_median_across_runs():
    runs = [
        {"infer_ms": {"p50": 1.0, "p95": 2.0, "p99": 3.0}},
        {"infer_ms": {"p50": 2.0, "p95": 4.0, "p99": 6.0}},
        {"infer_ms": {"p50": 3.0, "p95": 6.0, "p99": 9.0}},
    ]
    summary = MODULE.summarize(runs)
    assert summary["infer_ms"]["p50"]["median"] == 2.0
    assert summary["infer_ms"]["p95"]["median"] == 4.0
    assert summary["infer_ms"]["p99"]["median"] == 6.0
    assert summary["infer_ms"]["p50"]["n"] == 3


def test_summarize_marks_missing_metric():
    summary = MODULE.summarize([{"queue_wait_ms": {"p50": 1.0, "p95": 2.0, "p99": 3.0}}])
    assert summary["queue_wait_ms"]["p50"]["median"] == 1.0
    assert summary["infer_ms"]["p50"] is None
