"""Replay synthetic constant-rate video twice and verify frame identity and timing."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import cv2


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("exe","engine","images","out"):
        p.add_argument(name,type=Path)
    args=p.parse_args()
    files=sorted(args.images.glob("*.jpg"))
    if not files:
        raise ValueError("No replay images")
    args.out.mkdir(parents=True,exist_ok=False)
    source=args.out/"fixture.avi"
    first=cv2.imread(str(files[0]))
    size=first.shape[1::-1]
    writer=cv2.VideoWriter(str(source),cv2.VideoWriter_fourcc(*"MJPG"),25,size)
    assert writer.isOpened()
    for i in range(1000):
        frame=cv2.imread(str(files[i%len(files)]))
        assert frame.shape==first.shape
        writer.write(frame)
    writer.release()
    signatures=[]
    for name in ("first","second"):
        out=args.out/name
        subprocess.run([args.exe,args.engine,source,out],check=True)
        rows=[json.loads(line) for line in (out/"frames.jsonl").read_text().splitlines()]
        assert len(rows)==1000 and any(row["detections"] for row in rows)
        for i,row in enumerate(rows):
            assert row["frame_id"]==i and abs(row["source_ts_ns"]-i*40_000_000)<=1
            assert row["timestamp_source"]=="video_pts"

        # Stable semantic contract; wall-clock measurements are intentionally excluded.
        fields = ("frame_id", "source_ts_ns", "timestamp_source", "detections",
                  "tracks", "removed_ids", "selection")
        stable = [{key: row[key] for key in fields if key in row} for row in rows]
        signatures.append(hashlib.sha256(json.dumps(stable, sort_keys=True).encode()).hexdigest())
        cap=cv2.VideoCapture(str(out/"overlay.mp4"))
        decoded=0
        while cap.read()[0]:
            decoded+=1
        cap.release()
        assert decoded==1000
    assert signatures[0]==signatures[1]
    print("Verified two 1000-frame runs:",signatures[0])
    print((args.out/"first/summary.json").read_text())


if __name__=="__main__":
    main()
