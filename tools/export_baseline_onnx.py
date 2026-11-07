"""Export a fixed 320px FP32 graph and verify it against the PyTorch reference."""
import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
import onnx
import onnxruntime as ort
import torch
from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("weights", type=Path)
    parser.add_argument("val_images", type=Path)
    args = parser.parse_args()
    model = YOLO(str(args.weights))
    path = Path(model.export(format="onnx", imgsz=320, batch=1, opset=17, dynamic=False,
                             simplify=False, nms=False, half=False, device="cpu"))
    onnx.checker.check_model(str(path))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    net = YOLO(str(args.weights)).model.eval().fuse()
    files = sorted(args.val_images.glob("*.jpg"))
    samples = []
    for index in sorted({0, len(files)//2, len(files)-1}):
        image = files[index]
        rgb = cv2.cvtColor(cv2.imread(str(image)), cv2.COLOR_BGR2RGB)
        assert rgb.shape == (320, 320, 3)
        x = np.ascontiguousarray(rgb.transpose(2,0,1)[None],dtype=np.float32)/255
        with torch.inference_mode():
            reference = net(torch.from_numpy(x))[0].numpy()
        actual = session.run(None, {session.get_inputs()[0].name:x})[0]
        np.testing.assert_allclose(actual, reference, rtol=2e-4, atol=0.001)
        if index == 0:
            np.savez_compressed(path.parent/"onnx-reference.npz",input=x,output=reference)
        samples.append(dict(image=image.name, sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                            max_abs=float(np.max(np.abs(actual-reference)))))
    result = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),opset=17,
                  inputs=[(x.name,x.shape,x.type) for x in session.get_inputs()],
                  outputs=[(x.name,x.shape,x.type) for x in session.get_outputs()],
                  names=model.names, nms=False, samples=samples)
    (path.parent/"onnx-contract.json").write_text(json.dumps(result,indent=2))
    print(result)


if __name__=="__main__":
    main()
