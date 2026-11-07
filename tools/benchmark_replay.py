"""Repeat a vision_replay run and summarize latency percentiles.

The executable is invoked exactly as the normal replay entry point, so the
benchmark exercises decoder, queue, detector and tracker together.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any


METRICS = ("queue_wait_ms", "infer_ms", "mot_ms", "result_age_ms")
PERCENTILES = ("p50", "p95", "p99")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def summarize(runs: list[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for metric in METRICS:
        result[metric] = {}
        for percentile in PERCENTILES:
            values = [
                float(run[metric][percentile])
                for run in runs
                if metric in run and percentile in run[metric]
            ]
            if not values:
                result[metric][percentile] = None
                continue
            result[metric][percentile] = {
                "median": statistics.median(values),
                "min": min(values),
                "max": max(values),
                "n": len(values),
            }
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("engine", type=Path)
    parser.add_argument("video", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--tracker", type=Path)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--label", default="")
    args = parser.parse_args(argv)
    if args.runs < 1:
        parser.error("--runs must be >= 1")
    if args.out.exists():
        parser.error(f"output directory already exists: {args.out}")
    args.out.mkdir(parents=True)

    summaries = []
    for index in range(args.runs):
        run_dir = args.out / f"run-{index + 1}"
        command = [str(args.exe), str(args.engine), str(args.video), str(run_dir)]
        if args.tracker:
            command.append(str(args.tracker))
        if args.config:
            command += ["--config", str(args.config)]
        print("run", index + 1, " ".join(command))
        subprocess.run(command, check=True)
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        summaries.append(summary)

    report = {
        "label": args.label,
        "exe": str(args.exe),
        "exe_sha256": sha256(args.exe),
        "engine": str(args.engine),
        "engine_sha256": sha256(args.engine),
        "video": str(args.video),
        "video_sha256": sha256(args.video),
        "config": str(args.config) if args.config else None,
        "tracker": str(args.tracker) if args.tracker else None,
        "runs": summaries,
        "median": summarize(summaries),
    }
    (args.out / "benchmark.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report["median"], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
