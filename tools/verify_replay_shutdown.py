"""Inject overlay-open failure while realtime producer waits across a 60s gap."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

import cv2
import numpy as np


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("exe",type=Path)
    p.add_argument("engine",type=Path)
    p.add_argument("report",type=Path)
    args=p.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        root=Path(folder)
        source=root/"long-gap.avi"
        writer=cv2.VideoWriter(str(source),cv2.VideoWriter_fourcc(*"MJPG"),1/60,(320,320))
        assert writer.isOpened()
        for _ in range(2): writer.write(np.zeros((320,320,3),np.uint8))
        writer.release()
        capture=cv2.VideoCapture(str(source))
        fps=capture.get(cv2.CAP_PROP_FPS)
        capture.release()
        assert 1/fps>50
        config=root/"replay.yml"
        config.write_text("%YAML:1.0\nqueue_capacity: 1\nqueue_mode: latest\nrealtime_pace: 1\n")
        output=root/"result"
        start=time.monotonic()
        proc=subprocess.Popen([str(args.exe.resolve()),str(args.engine.resolve()),str(source),str(output),
                               "--config",str(config)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            while not output.exists() and proc.poll() is None and time.monotonic()-start<5:
                time.sleep(.001)
            assert output.exists()
            (output/"overlay.mp4").mkdir()  # Force VideoWriter failure in the consumer.
            try:
                stdout,stderr=proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                stdout,stderr=proc.communicate()
                args.report.with_suffix(".failure.txt").write_text(
                    str(list(output.iterdir()))+"\n"+stderr.decode(errors="replace"))
                raise
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()
        elapsed=time.monotonic()-start
        summary=json.loads((output/"summary.json").read_text())
        assert proc.returncode!=0 and summary["status"]=="error"
        assert summary["error"]=="Cannot open overlay writer"
        assert elapsed<5 and summary["captured"]==1
        result={"exit_seconds":elapsed,"source_frame_gap_seconds":1/fps,"summary":summary}
        args.report.write_text(json.dumps(result,indent=2))
        print("Consumer failure interrupted 60-second pacing wait in",elapsed,"seconds")


if __name__=="__main__":
    main()
