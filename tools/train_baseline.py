"""Train the 320px YOLO11n baseline and evaluate the best weights."""
import argparse
import hashlib
import json
from pathlib import Path

import torch
import ultralytics
from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("weights", "data", "run"):
        parser.add_argument(key, type=Path)
    args = parser.parse_args()
    run = args.run.resolve()
    if run.exists():
        raise FileExistsError(run)
    model = YOLO(str(args.weights))
    model.train(data=str(args.data.resolve()), project=str(run.parent), name=run.name,
                imgsz=320, epochs=30, patience=8, batch=8, workers=0, device=0,
                amp=False, cache="ram", seed=0, optimizer="AdamW", lr0=0.001,
                deterministic=True, plots=False)
    evaluate(run, args.weights, args.data)

def evaluate(run, weights, data):
    best = run / "weights/best.pt"
    metrics = YOLO(str(best)).val(data=str(data.resolve()), imgsz=320, batch=8,
                                  device=0, workers=0, plots=False,
                                  project=str(run), name="best-validation")
    summary = dict(torch=str(torch.__version__), ultralytics=ultralytics.__version__,
                   gpu=torch.cuda.get_device_name(0), initial_sha256=hashlib.sha256(weights.read_bytes()).hexdigest(),
                   best_sha256=hashlib.sha256(best.read_bytes()).hexdigest(), overall=metrics.results_dict,
                   classes=metrics.summary())
    (run/"baseline.json").write_text(json.dumps(summary, indent=2, default=lambda value: value.item()), encoding="utf-8")


if __name__ == "__main__":
    main()

