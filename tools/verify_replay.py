"""Replay synthetic constant-rate video twice and verify frame identity and timing."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import cv2


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("exe","engine","images","out"):
        p.add_argument(name,type=Path)
    p.add_argument("--tracker",type=Path,help="Explicit tracker configuration")
    p.add_argument("--reference-frames",type=Path,help="Compare stable fields to an earlier frames.jsonl")
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
        command=[args.exe,args.engine,source,out]
        if args.tracker:
            command.append(args.tracker)
        subprocess.run(command,check=True)
        rows=[json.loads(line) for line in (out/"frames.jsonl").read_text().splitlines()]
        assert len(rows)==1000 and any(row["detections"] for row in rows)
        for i,row in enumerate(rows):
            assert row["frame_id"]==i and abs(row["source_ts_ns"]-i*40_000_000)<=1
            assert row["timestamp_source"]=="video_pts"
            for track in row["tracks"]:
                if "mode_probabilities" in track:
                    probabilities=track["mode_probabilities"]
                    assert len(probabilities)==3
                    assert all(math.isfinite(p) and 0<=p<=1 for p in probabilities)
                    assert abs(sum(probabilities)-1)<1e-5

        # Stable semantic contract; wall-clock measurements are intentionally excluded.
        fields = ("frame_id", "source_ts_ns", "timestamp_source", "detections",
                  "tracks", "removed_ids", "selection")
        stable = [{key: row[key] for key in fields if key in row} for row in rows]
        if args.reference_frames:
            reference=[json.loads(line) for line in args.reference_frames.read_text().splitlines()]
            assert stable==[{key: row[key] for key in fields if key in row} for row in reference]
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
