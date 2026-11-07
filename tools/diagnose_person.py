import json
from pathlib import Path
from collections import Counter
import cv2
import numpy as np
import onnxruntime as ort
from verify_trt import decode, overlap

root=Path("artifacts/datasets/yolo_v002")
run=Path("artifacts/runs/yolo11n-v002-320")
session=ort.InferenceSession(str(run/"weights/best.onnx"),providers=["CPUExecutionProvider"])
samples=[]
gt_by_split={}
for split in ("train","val"):
    heights=[]
    for p in sorted((root/"labels"/split).glob("*.txt")):
        gt=[]
        for line in p.read_text().splitlines():
            c,x,y,w,h=map(float,line.split())
            if c!=1:continue
            heights.append(h*320)
            gt.append(np.array([x-w/2,y-h/2,x+w/2,y+h/2])*320)
        if split=="val":
            im=cv2.imread(str(root/"images/val"/p.with_suffix(".jpg").name))
            x=np.ascontiguousarray(im[:,:,::-1].transpose(2,0,1)[None],dtype=np.float32)/255
            pred=decode(session.run(None,{"images":x})[0],.001,.45)
            samples.append((gt,[(s,b) for c,s,b in pred if c==1]))
    gt_by_split[split]=dict(median_height_px=float(np.median(heights)),below32=sum(h<32 for h in heights),total=len(heights))
def evaluate(conf,cut):
    tp=fp=fn=0
    buckets=Counter()
    for gt,pred in samples:
        remaining=set(range(len(gt)))
        for score,b in pred:
            if score<conf: continue
            v,i=max(((overlap(b,gt[i]),i) for i in remaining),default=(0,None))
            if v>=cut:
                remaining.remove(i);tp+=1
            else:fp+=1
        fn+=len(remaining)
        for i,g in enumerate(gt):
            group="under32" if g[3]-g[1]<32 else "32plus"
            buckets[group+"_total"]+=1
            buckets[group+"_missed"]+=int(i in remaining)
    return dict(conf=conf,iou=cut,tp=tp,fp=fp,fn=fn,recall=tp/(tp+fn),precision=tp/max(1,tp+fp),size=buckets)
report=dict(sizes=gt_by_split,operating_points=[evaluate(c,.5) for c in (.5,.25,.1,.05,.01,.001)],looser_iou=evaluate(.25,.3))
(run/"person-diagnosis.json").write_text(json.dumps(report,indent=2))
print(json.dumps(report))
# Draw missed people at the deployment comparison point.
from PIL import Image, ImageDraw
panels = []
for path,(gt,pred) in zip(sorted((root/"labels/val").glob("*.txt")),samples):
    remaining = set(range(len(gt)))
    for score,box in pred:
        if score < .25:
            continue
        iou,i = max(((overlap(box,gt[i]),i) for i in remaining), default=(0,None))
        if iou >= .5:
            remaining.remove(i)
    if not remaining:
        continue
    with Image.open(root/"images/val"/path.with_suffix(".jpg").name) as src:
        panel = src.convert("RGB")
    draw = ImageDraw.Draw(panel)
    for score,box in pred:
        if score >= .25:
            draw.rectangle(tuple(box), outline="cyan")
    for i in remaining:
        draw.rectangle(tuple(gt[i]),outline="yellow",width=2)
    draw.text((3,3),path.stem,fill="white",stroke_width=1,stroke_fill="black")
    panels.append(panel)
if panels:
    sheet = Image.new("RGB",(3*320,((len(panels)+2)//3)*320))
    for i,panel in enumerate(panels):
        sheet.paste(panel,(i%3*320,i//3*320))
    sheet.save(run/"person-misses.jpg",quality=92)
