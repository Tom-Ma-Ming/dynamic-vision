"""Export public YOLO11n to a fixed 320 ONNX graph.

This is the COCO checkpoint, not the project's yolo_v002 fine-tune. The
fine-tune weights are not in git. Output is 1x3x320x320 in and the usual
channel-major YOLO head, with NMS left to the runner.
"""
import argparse
from pathlib import Path

from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("weights", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    model = YOLO(str(args.weights))
    exported = Path(model.export(format="onnx", imgsz=320, batch=1, opset=17, dynamic=False,
                                 simplify=False, nms=False, half=False, device="cpu"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    exported.replace(args.output)
    print(args.output)


if __name__ == "__main__":
    main()
