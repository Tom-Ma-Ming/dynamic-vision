"""Verify fixed-window simulation reports and independently recompute their errors."""
import argparse
import collections
import json
from pathlib import Path
import numpy as np


def verify(directory):
    rows = collections.defaultdict(list)
    for line in (directory / "runs.jsonl").read_text().splitlines():
        row = json.loads(line)
        rows[row["scene"]].append(row)
    report = json.loads((directory / "summary.json").read_text())
    for scene in report["scenarios"]:
        frames = rows[scene["id"]]
        metrics = scene["metrics"]
        assert len(frames) == metrics["frames"] == scene["planned_frames"]
        errors = [np.hypot(r["aim_x"]-r["truth_x"],r["aim_y"]-r["truth_y"]) for r in frames]
        np.testing.assert_allclose(np.mean(errors),metrics["control_mae_px"],atol=.001)
        available = [r for r in frames if r["has_track"]]
        assert len(available) == metrics["tracked_frames"]
        np.testing.assert_allclose(len(available)/len(frames),metrics["track_coverage"],atol=1e-8)
        if available:
            estimate = [np.hypot(r["est_x"]-r["truth_x"],r["est_y"]-r["truth_y"]) for r in available]
            np.testing.assert_allclose(np.mean(estimate),metrics["estimate_mae_px"],atol=.001)
        else:
            assert metrics["estimate_mae_px"] is None
        horizon_ns=round(scene["prediction_horizon_s"]*1e9)
        times = np.array([r["ts_ns"] for r in frames])
        valid = [r for r in available if r["ts_ns"]+horizon_ns<=times[-1]]
        assert len(valid) == metrics["prediction_samples"]
        if valid:
            future = np.array([r["ts_ns"]+horizon_ns for r in valid])
            x=np.interp(future,times,[r["truth_x"] for r in frames])
            y=np.interp(future,times,[r["truth_y"] for r in frames])
            error=np.hypot(np.array([r["pred_x"] for r in valid])-x,np.array([r["pred_y"] for r in valid])-y)
            np.testing.assert_allclose(error.mean(),metrics["prediction_mae_px"],atol=.001)
        else:
            assert metrics["prediction_mae_px"] is None
    return rows


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("base",type=Path)
    p.add_argument("experiment",type=Path)
    a=p.parse_args()
    base,experiment=verify(a.base),verify(a.experiment)
    assert base.keys()==experiment.keys()
    for name in base:
        assert len(base[name])==len(experiment[name])
        for x,y in zip(base[name],experiment[name]):
            for k in ("frame","ts_ns","truth_x","truth_y","observed","detections"):
                assert x[k]==y[k]
    print("Verified fixed windows, identical observations, full control error, coverage and future prediction error")
