"""Honest model probe for SA04.

Runs a CPU ONNX graph when one is on disk. Does not treat that as TensorRT,
and does not treat a missing project engine as a pass.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

WINDOWS_COMMAND = (
    "cmake --build --preset windows-sdk --target vision_sim_image\n"
    "out\\windows-sdk\\apps\\trt_infer\\Release\\vision_sim_image.exe "
    "artifacts\\engines\\yolo11n-v002-retrain-20261110-fp32\\best-fp32.engine "
    "<sprite> <background> configs\\tracker-body-primary.yml <NEW_DIR> "
    "19 42.4 431.2 674.9 1 1 0 0 0 0 human_led"
)


def sha256(path):
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def letterbox(image, canvas=320):
    import numpy as np
    array = np.asarray(image.convert("RGB"), dtype=np.float32) / 255.0
    height, width = array.shape[:2]
    scale = canvas / max(width, height)
    resized_w = max(1, int(round(width * scale)))
    resized_h = max(1, int(round(height * scale)))
    y_index = (np.arange(resized_h) / scale).astype(np.int32).clip(0, height - 1)
    x_index = (np.arange(resized_w) / scale).astype(np.int32).clip(0, width - 1)
    resized = array[y_index][:, x_index]
    canvas_image = np.full((canvas, canvas, 3), 114 / 255.0, dtype=np.float32)
    top = (canvas - resized_h) // 2
    left = (canvas - resized_w) // 2
    canvas_image[top:top + resized_h, left:left + resized_w] = resized
    return np.transpose(canvas_image, (2, 0, 1))[None, ...]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit("output directory must be new")
    args.out.mkdir(parents=True)
    root = args.root
    nvidia = (root / "/dev/nvidia0").exists() or Path("/dev/nvidia0").exists() or Path("/dev/nvidiactl").exists()
    project_onnx = root / "artifacts/runs/yolo11n-v002-retrain-20261110-320/weights/best.onnx"
    project_engine = root / "artifacts/engines/yolo11n-v002-retrain-20261110-fp32/best-fp32.engine"
    coco = root / "models/yolo11n-coco-320.onnx"
    image = root / "models/yolo-loop/person.jpg"
    cpu = {"status": "blocked", "result": "UNKNOWN", "project_contract": False}
    try:
        import onnxruntime
    except ImportError:
        onnxruntime = None
        cpu["reason"] = "onnxruntime is not installed"
    if onnxruntime is not None and coco.is_file() and image.is_file():
        import numpy as np
        from PIL import Image
        session = onnxruntime.InferenceSession(str(coco), providers=["CPUExecutionProvider"])
        tensor = letterbox(Image.open(image))
        output = session.run(None, {session.get_inputs()[0].name: tensor})[0]
        cpu = {
            "status": "executed",
            "result": "executed_coco_graph_not_project_contract",
            "provider": "CPUExecutionProvider",
            "model": str(coco),
            "model_sha256": sha256(coco),
            "image": str(image),
            "input_shape": list(tensor.shape),
            "output_shape": list(output.shape),
            "output_finite": bool(np.isfinite(output).all()),
            "project_contract": list(output.shape)[1:2] == [6],
            "note": "Public COCO YOLO11n. The project head/person best.onnx is not in this checkout.",
        }
    elif onnxruntime is not None:
        cpu["reason"] = "CPU runtime is present but the ONNX file or probe image is missing"
    report = {
        "passed": False,
        "unknown_counted_as_pass": False,
        "overall": "UNKNOWN",
        "cpu_onnx": cpu,
        "project_onnx": {
            "path": str(project_onnx),
            "status": "present" if project_onnx.is_file() else "MISSING",
            "sha256": sha256(project_onnx) if project_onnx.is_file() else None,
        },
        "project_engine": {
            "path": str(project_engine),
            "status": "present" if project_engine.is_file() else "MISSING",
            "sha256": sha256(project_engine) if project_engine.is_file() else None,
        },
        "tensorrt": {
            "status": "blocked",
            "result": "UNKNOWN",
            "gpu_present": nvidia,
            "reason": "This machine has no NVIDIA GPU node. CMake refuses VISION_WITH_TRT off Windows. The project engine file is not in this checkout. CPU ONNX is not a TensorRT pass.",
            "windows_command": WINDOWS_COMMAND,
            "windows_command_note": "vision_sim_image human_led is the existing trim, not this round's fixed_pid comparison. The off twin replaces the last word with human_led_off. Neither command was executed here.",
        },
    }
    (args.out / "model-probe.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (args.out / "report.md").write_text(
        "# SA04 模型探针\n\n"
        f"CPU ONNX：{cpu.get('status')} / {cpu.get('result')}\n\n"
        "TensorRT：UNKNOWN / blocked。没有把缺 GPU 记成通过。\n\n"
        "Windows 命令（未执行）：\n\n```\n" + WINDOWS_COMMAND + "\n```\n",
        encoding="utf-8")
    print(json.dumps({"overall": "UNKNOWN", "cpu_onnx": cpu.get("status"), "tensorrt": "UNKNOWN", "passed": False}))
    return 2


if __name__ == "__main__":
    sys.exit(main())
