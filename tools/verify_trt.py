"""Check the C++ TensorRT runner against ONNX on the fixed export fixtures."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

import cv2
import numpy as np
import onnxruntime as ort


def overlap(a, b):
    extent = np.maximum(0, np.minimum(a[2:], b[2:])-np.maximum(a[:2], b[:2]))
    area = extent.prod()
    return float(area / ((a[2:]-a[:2]).prod() + (b[2:]-b[:2]).prod() - area))


def decode(tensor, confidence, nms_iou):
    keep = []
    for row in tensor[0].T:
        cls = int(row[4:].argmax())
        score = float(row[4+cls])
        if score >= confidence:
            half = row[2:4]/2
            keep.append((cls, score, np.concatenate((row[:2]-half, row[:2]+half))))
    result = []
    for cls, score, box in sorted(keep, key=lambda item: -item[1]):
        if all(c != cls or overlap(box,b) <= nms_iou for c,_,b in result):
            result.append((cls,score,box))
    return result


def compare(onnx_boxes, engine_boxes):
    unmatched = list(engine_boxes)
    details = []
    for cls, score, box in onnx_boxes:
        candidates = [(overlap(box,b),i) for i,(c,_,b) in enumerate(unmatched) if c==cls]
        if not candidates:
            raise ValueError("Missing detection")
        iou, i = max(candidates)
        _, engine_score, engine_box = unmatched.pop(i)
        if iou < .99 or abs(score-engine_score) > .001:
            raise ValueError(f"Detection mismatch: IoU={iou}, score delta={score-engine_score}")
        details.append(dict(cls=cls,score=engine_score,xyxy=engine_box.tolist(),iou=iou))
    if unmatched:
        raise ValueError("Extra TensorRT detections")
    return details


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe","engine","onnx","images"):
        parser.add_argument(key,type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.engine.parent/"manifest.json").read_text(encoding="utf-8-sig"))
    for path,field in ((args.engine,"engine_sha256"),(args.onnx,"onnx_sha256")):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=manifest[field].lower():
            raise ValueError(f"SHA mismatch: {path}")
    contract = json.loads((args.onnx.parent/"onnx-contract.json").read_text())
    session = ort.InferenceSession(str(args.onnx),providers=["CPUExecutionProvider"])
    results = []
    with TemporaryDirectory() as tmp:
        raw = Path(tmp)/"input.bin"
        output = Path(tmp)/"output.bin"
        for sample in contract["samples"]:
            path = args.images/sample["image"]
            if hashlib.sha256(path.read_bytes()).hexdigest()!=sample["sha256"]:
                raise ValueError(f"Fixture changed: {path}")
            img = cv2.imread(str(path))
            if img.shape != (320,320,3):
                raise ValueError("Expected native 320px fixture")
            x = np.ascontiguousarray(img[:,:,::-1].transpose(2,0,1)[None],dtype=np.float32)/255
            x.tofile(raw)
            subprocess.run([args.exe,args.engine,raw,output],check=True)
            reference = session.run(["output0"],{"images":x})[0]
            actual = np.fromfile(output,np.float32).reshape(reference.shape)
            np.testing.assert_allclose(actual,reference,atol=.01,rtol=.0002,equal_nan=False)
            details = compare(decode(reference,.25,.45),decode(actual,.25,.45))
            results.append(dict(image=path.name,max_abs=float(np.abs(actual-reference).max()),
                                detections=details))
        if not any(r["detections"] for r in results):
            raise ValueError("Fixtures produced no boxes")
    report=dict(confidence=.25,nms_iou=.45,raw_atol=.01,raw_rtol=.0002,samples=results)
    (args.engine.parent/"verification.json").write_text(json.dumps(report,indent=2))
    print([(r["image"],r["max_abs"],len(r["detections"])) for r in results])


if __name__=="__main__":
    main()
