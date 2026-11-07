"""Run native JPEG and odd-aspect letterbox checks against ONNX Runtime."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import cv2
import numpy as np
import onnxruntime as ort
from verify_trt import compare, decode


def check(exe, engine, session, path):
    image = cv2.imread(str(path))
    h,w = image.shape[:2]
    scale = 320/max(w,h)
    rw,rh = max(1,round(w*scale)),max(1,round(h*scale))
    left,top = (320-rw)//2,(320-rh)//2
    canvas = np.full((320,320,3),114,np.uint8)
    canvas[top:top+rh,left:left+rw] = cv2.resize(image,(rw,rh))
    x = np.ascontiguousarray(canvas[:,:,::-1].transpose(2,0,1)[None],dtype=np.float32)/255
    raw = session.run(None,{"images":x})[0]
    expected = []
    for c,s,b in decode(raw,.25,.45):
        box = np.clip((b-[left,top,left,top])*[w/rw,h/rh,w/rw,h/rh],0,[w,h,w,h])
        if min(box[2:]-box[:2])>0:
            expected.append((c,s,box))
    result = subprocess.run([exe,engine,path],check=True,capture_output=True,text=True)
    actual = [(d["class_id"],d["score"],np.array(d["xyxy"])) for d in json.loads(result.stdout)]
    matches = compare(expected,actual)
    err = max((float(np.max(np.abs(box-np.array(m["xyxy"])))) for (_,_,box),m in zip(expected,matches)),default=0)
    assert err < 1
    return dict(image=path.name,boxes=len(matches),max_px=err)


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("exe","engine","onnx","images","report"):
        p.add_argument(name,type=Path)
    a=p.parse_args()
    session=ort.InferenceSession(str(a.onnx),providers=["CPUExecutionProvider"])
    files=sorted(a.images.glob("*.jpg"))
    samples=[files[i] for i in (0,len(files)//2,-1)]
    results=[check(a.exe,a.engine,session,image) for image in samples]
    with tempfile.TemporaryDirectory() as t:
        for width,height in ((401,321),(321,401)):
            path=Path(t)/f"{width}x{height}.png"
            canvas=np.full((height,width,3),114,np.uint8)
            canvas[:320,:320]=cv2.imread(str(files[-1]))
            assert cv2.imwrite(str(path),canvas)
            results.append(check(a.exe,a.engine,session,path))
    assert all(r["boxes"] for r in results)
    a.report.write_text(json.dumps(results,indent=2))
    print(results)
