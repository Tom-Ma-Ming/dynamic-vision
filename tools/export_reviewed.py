"""Export reviewed X-AnyLabeling rectangles to a new YOLO dataset."""
import argparse
import csv
import hashlib
import json
import math
import shutil
from collections import Counter
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image

NAMES = ("head", "person")


def export(source, destination):
    destination = destination.resolve()
    if destination.exists():
        raise FileExistsError(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    report = {}
    audit = []
    with TemporaryDirectory(dir=destination.parent) as staging:
        root = Path(staging) / "dataset"
        for split in ("train", "val"):
            folder = source / split
            jpgs = sorted(folder.glob("*.jpg"))
            if not jpgs or {p.stem for p in jpgs} != {p.stem for p in folder.glob("*.json")}:
                raise ValueError(f"{folder}: missing image/JSON pairs")
            counts = Counter()
            clipped = 0
            for sub in ("images", "labels"):
                (root / sub / split).mkdir(parents=True)
            for p in jpgs:
                meta = json.loads(p.with_suffix(".json").read_text(encoding="utf-8-sig"))
                with Image.open(p) as im:
                    im.load()
                    w, h = im.size
                if (meta["imageWidth"], meta["imageHeight"], meta["imagePath"]) != (w, h, p.name):
                    raise ValueError(f"{p}: image metadata mismatch")
                sha = hashlib.sha256(p.read_bytes()).digest()
                if sha in seen:
                    raise ValueError(f"{p}: repeated image")
                seen.add(sha)
                boxes = []
                lines = []
                for shape in meta["shapes"]:
                    name = shape["label"]
                    cls = NAMES.index(name)
                    pts = shape["points"]
                    if shape["shape_type"] != "rectangle" or len(pts) not in (2,4):
                        raise ValueError(f"{p}: unexpected shape")
                    xs, ys = zip(*pts)
                    if not all(math.isfinite(v) for v in (*xs,*ys)):
                        raise ValueError(f"{p}: nonfinite coordinate")
                    l,r,t,b = min(xs),max(xs),min(ys),max(ys)
                    overshoot = max(0,-l,-t,r-w,b-h)
                    if 0 < overshoot <= 0.001:
                        l,t,r,b = max(0,l),max(0,t),min(w,r),min(h,b)
                        clipped += 1
                    if not (0 <= l < r <= w and 0 <= t < b <= h):
                        raise ValueError(f"{p}: box outside image {(l,t,r,b)}")
                    boxes.append((cls,l,t,r,b))
                    lines.append(f"{cls} {(l+r)/(2*w):.10f} {(t+b)/(2*h):.10f} {(r-l)/w:.10f} {(b-t)/h:.10f}\n")
                    counts[name] += 1
                for i,(cls,l,t,r,b) in enumerate(boxes,1):
                    if cls == 0:
                        gaps = [max(0,L-l,T-t,r-R,b-B) for c,L,T,R,B in boxes if c==1]
                        if not gaps or min(gaps)>0.00001:
                            audit.append((split,p.name,i,round(min(gaps),4) if gaps else "head_only"))
                shutil.copyfile(p,root/"images"/split/p.name)
                (root/"labels"/split/p.with_suffix(".txt").name).write_text("".join(lines))
            report[split] = dict(images=len(jpgs),boxes=dict(counts), edge_rounding=clipped)
        with (root/"containment.csv").open("w",newline="",encoding="utf-8-sig") as f:
            writer=csv.writer(f)
            writer.writerow(["split","image","head_line","edge_overflow_px"])
            writer.writerows(audit)
        (root/"dataset.yaml").write_text(
            f"path: {json.dumps(destination.as_posix())}\ntrain: images/train\nval: images/val\nnames:\n  0: head\n  1: person\n")
        (root/"summary.json").write_text(json.dumps(report,indent=2))
        root.rename(destination)
    print(report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    parser.add_argument("destination",type=Path)
    args = parser.parse_args()
    export(args.source,args.destination)

