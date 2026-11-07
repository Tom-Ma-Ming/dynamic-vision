import json
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "eval_report.py"


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def make_fixture(root: Path, complete: bool = True) -> Path:
    artifacts = root / "artifacts"
    run = artifacts / "runs" / "yolo11n-v002-320"
    write_json(
        run / "baseline.json",
        {
            "overall": {"metrics/mAP50(B)": 0.5, "metrics/mAP50-95(B)": 0.2},
            "classes": [
                {"Class": "head", "Box-P": 0.8, "Box-R": 0.6, "mAP50": 0.5, "mAP50-95": 0.2},
                {"Class": "person", "Box-P": 0.7, "Box-R": 0.7, "mAP50": 0.6, "mAP50-95": 0.2},
            ],
        },
    )
    write_json(
        run / "person-diagnosis.json",
        {
            "sizes": {
                "train": {"median_height_px": 40, "below32": 10, "total": 100},
                "val": {"median_height_px": 50, "below32": 2, "total": 10},
            },
            "operating_points": [
                {
                    "conf": 0.25,
                    "iou": 0.5,
                    "tp": 7,
                    "fp": 2,
                    "fn": 3,
                    "recall": 0.7,
                    "precision": 0.777,
                    "size": {
                        "under32_total": 2,
                        "under32_missed": 1,
                        "32plus_total": 8,
                        "32plus_missed": 2,
                    },
                }
            ],
            "looser_iou": {"conf": 0.25, "iou": 0.3, "tp": 8, "fp": 1, "fn": 2},
        },
    )
    write_json(
        run / "weights" / "onnx-contract.json",
        {"inputs": [["images", [1, 3, 320, 320], "tensor(float)"]],
         "outputs": [["output0", [1, 6, 2100], "tensor(float)"]]},
    )
    write_json(
        artifacts / "datasets" / "yolo_v002" / "summary.json",
        {"train": {"images": 10, "boxes": {"head": 11, "person": 12}},
         "val": {"images": 2, "boxes": {"head": 2, "person": 2}}},
    )
    write_json(
        artifacts / "validation-20261102-000654" / "labeled-review" / "quality.json",
        {"empty_labels": 0, "invalid_images": 0},
    )
    if complete:
        write_json(
            artifacts / "engines" / "yolo11n-v002-fp32" / "manifest.json",
            {"gpu": "fixture GPU", "engine_sha256": "00", "onnx_sha256": "11"},
        )
        write_json(
            artifacts / "engines" / "yolo11n-v002-fp32" / "verification.json",
            {"samples": [{"max_abs": 0.001, "detections": [{"cls": 1}]}]},
        )
        write_json(
            artifacts / "engines" / "yolo11n-v002-fp32" / "image-check.json",
            [{"max_px": 0.0001, "boxes": 1}],
        )
        write_json(
            artifacts / "replay" / "mot-v1" / "synthetic-metrics.json",
            {"frames": 36, "HOTA": 0.9, "IDF1": 0.95, "IDSW": 0},
        )
        base = {
            "frames": 1000,
            "captured": 1000,
            "processed": 1000,
            "replaced": 0,
            "rejected": 0,
            "pending": 0,
            "stale": 0,
            "failed": 0,
            "peak": 1,
            "result_age_ms": {"p50": 1.0, "p95": 2.0, "p99": 3.0, "max": 4.0, "n": 1000},
        }
        write_json(artifacts / "track02" / "blocking-summary.json",
                   base | {"capacity": 1, "mode": "blocking"})
        write_json(artifacts / "track02" / "realtime-summary.json",
                   base | {"capacity": 1, "mode": "latest", "paced": True})
        write_json(
            artifacts / "track02" / "realtime-overload-summary.json",
            base | {"processed": 250, "replaced": 750, "capacity": 1, "mode": "latest",
                    "paced": True},
        )
        write_text(artifacts / "track02" / "cpu-ctest.log",
                   "100% tests passed, 0 tests failed out of 20\n")
        write_text(artifacts / "track02" / "windows-sdk-ctest.log",
                   "100% tests passed, 0 tests failed out of 3\n")
        write_text(artifacts / "track02" / "windows-full-ctest.log",
                   "100% tests passed, 0 tests failed out of 20\n")
        write_text(artifacts / "track02" / "pytest.log", "5 passed in 0.1s\n")
        write_text(artifacts / "track02" / "verify-replay.txt",
                   "Verified two 1000-frame runs: abc123\n")
        write_text(artifacts / "track02" / "blocking-compare.txt",
                   "baseline_frames=1000 new_frames=1000 stable_equal=True\n")
        write_text(
            artifacts / "track02" / "realtime-invariants.txt",
            "captured=1000\nprocessed=1000\nreplaced=0\npeak=1\nconservation=True\n",
        )
        write_text(
            artifacts / "track02" / "realtime-overload-invariants.txt",
            "captured=1000\nprocessed=250\nreplaced=750\npeak=1\nconservation=True\n",
        )
    write_text(
        root / "docs" / "environment-lock.md",
        "# Environment\n\n| 项目 | 本机实测 |\n|---|---|\n| GPU | fixture GPU |\n",
    )
    return artifacts


def run(artifacts: Path, out: Path, *extra: str):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(artifacts), "--out", str(out), *extra],
        capture_output=True,
        text=True,
    )


def test_complete_fixture_reports_all_layers(tmp_path):
    artifacts = make_fixture(tmp_path)
    out = tmp_path / "report"
    result = run(artifacts, out, "--strict")
    assert result.returncode == 0, result.stderr

    report = json.loads((out / "report.json").read_text(encoding="utf-8"))
    assert report["summary"]["status"] == "complete-with-gaps"
    assert report["missing_required"] == []
    assert report["detection"]["person_recall"] == 0.7
    assert report["engine"]["verification_max_abs"] == 0.001
    assert report["mot"]["HOTA"] == 0.9
    assert len(report["replay"]) == 3
    assert report["evidence"]["cpu_ctest"]["total"] == 20
    assert report["evidence"]["cpu_ctest"]["status"] == "passed"
    assert report["evidence"]["pytest"]["passed"] == 5
    assert len(report["gates"]) == 12
    assert next(g for g in report["gates"] if g["id"] == "T07")["status"] == "pass"
    assert next(g for g in report["gates"] if g["id"] == "T04")["status"] == "missing"
    assert (out / "report.md").exists()
    assert (out / "report.html").exists()
    assert all(len(entry["sha256"]) == 64 for entry in report["inputs"])


def test_missing_evidence_is_reported_and_strict_fails(tmp_path):
    artifacts = make_fixture(tmp_path, complete=False)
    out = tmp_path / "report"
    result = run(artifacts, out)
    assert result.returncode == 0, result.stderr

    report = json.loads((out / "report.json").read_text(encoding="utf-8"))
    assert report["summary"]["status"] == "incomplete"
    assert "engine.manifest" in report["missing_required"]
    assert report["engine"] is None
    assert report["mot"] is None
    assert (out / "report.md").exists()

    strict_out = tmp_path / "strict-report"
    strict = run(artifacts, strict_out, "--strict")
    assert strict.returncode == 1
    assert "missing required evidence" in strict.stderr


def test_output_directory_must_be_new_or_empty(tmp_path):
    artifacts = make_fixture(tmp_path)
    out = tmp_path / "report"
    out.mkdir()
    (out / "existing.txt").write_text("x", encoding="utf-8")
    result = run(artifacts, out)
    assert result.returncode != 0
    assert "output directory" in result.stderr
