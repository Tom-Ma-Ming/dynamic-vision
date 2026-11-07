"""Score frame-aligned GT and confirmed tracks ([id,x1,y1,x2,y2]) using TrackEval."""
import argparse
import json
from pathlib import Path
import numpy as np
from trackeval.metrics import HOTA, CLEAR, Identity


def score(rows):
    data = {"num_timesteps": len(rows)}
    boxes = {}
    for name, key in (("gt", "gt"), ("tracker", "tracks")):
        ids = sorted({box[0] for row in rows for box in row[key]})
        index = {v:i for i,v in enumerate(ids)}
        data[f"num_{name}_ids"] = len(ids)
        data[f"num_{name}_dets"] = sum(len(row[key]) for row in rows)
        data[f"{name}_ids"] = [np.array([index[b[0]] for b in row[key]],int) for row in rows]
        boxes[name] = [np.asarray([b[1:] for b in row[key]]).reshape(-1,4) for row in rows]
    data["similarity_scores"] = []
    for gt,pred in zip(boxes["gt"], boxes["tracker"]):
        a,b = gt[:,None,:],pred[None,:,:]
        wh = np.maximum(0,np.minimum(a[...,2:],b[...,2:])-np.maximum(a[...,:2],b[...,:2]))
        intersection = wh.prod(axis=2)
        union = (a[...,2:]-a[...,:2]).prod(axis=2)+(b[...,2:]-b[...,:2]).prod(axis=2)-intersection
        data["similarity_scores"].append(intersection/np.maximum(union,1e-12))
    settings = {"PRINT_CONFIG":False,"THRESHOLD":.5}
    return {"frames":len(rows),"HOTA":float(HOTA().eval_sequence(data)["HOTA"].mean()),
            "IDF1":float(Identity(settings).eval_sequence(data)["IDF1"]),
            "IDSW":int(CLEAR(settings).eval_sequence(data)["IDSW"])}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("input",type=Path)
    p.add_argument("output",type=Path)
    a=p.parse_args()
    rows=[json.loads(line) for line in a.input.read_text().splitlines()]
    result=score(rows)
    a.output.write_text(json.dumps(result,indent=2))
    print(result)
