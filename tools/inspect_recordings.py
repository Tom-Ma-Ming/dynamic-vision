"""Read-only video intake: full decode, media timestamps, hashes and contact sheets."""
import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np


def inspect(source, output):
    cap = cv2.VideoCapture(str(source))
    if not cap.isOpened():
        raise ValueError(f"Cannot open {source}")
    fps = cap.get(cv2.CAP_PROP_FPS)
    advertised = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    width, height = int(cap.get(3)), int(cap.get(4))
    targets = set(np.linspace(0, max(0, advertised-1), 12, dtype=int).tolist())
    samples, times = [], []
    previous = None
    duplicates = count = 0
    longest = streak = 0
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            if frame.shape[:2] != (height, width):
                raise ValueError(f"Frame size changed in {source} at {count}")
            times.append(float(cap.get(cv2.CAP_PROP_POS_MSEC)))
            same = previous is not None and np.array_equal(frame, previous)
            duplicates += int(same)
            streak = streak+1 if same else 0
            longest = max(longest, streak)
            previous = frame
            if count in targets:
                tile = cv2.resize(frame, (320, 320))
                tile = cv2.copyMakeBorder(tile, 26, 0, 0, 0, cv2.BORDER_CONSTANT)
                cv2.putText(tile, f"frame {count} | {times[-1]/1000:.2f}s", (7, 18),
                            cv2.FONT_HERSHEY_SIMPLEX, .45, (255,255,255), 1)
                samples.append(tile)
            count += 1
    finally:
        cap.release()
    if not count:
        raise ValueError(f"No decodable frames: {source}")
    while len(samples) < 12:
        samples.append(np.zeros((346, 320, 3), dtype=np.uint8))
    sheet = np.vstack([np.hstack(samples[i:i+4]) for i in range(0, 12, 4)])
    if not cv2.imwrite(str(output / f"{source.stem}-contact.jpg"), sheet):
        raise IOError("Cannot write contact sheet")
    differences = np.diff(times)
    with source.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    result = dict(source=str(source.resolve()), bytes=source.stat().st_size,
                  sha256=digest, width=width, height=height, nominal_fps=fps,
                  advertised_frames=advertised, decoded_frames=count,
                  frame_count_matches=count == advertised,
                  nominal_duration_s=count/fps if fps > 0 else None,
                  first_media_ms=times[0], last_media_ms=times[-1],
                  nonincreasing_media_timestamps=int(np.sum(differences <= 0)),
                  adjacent_identical_pairs=duplicates,
                  longest_identical_run_frames=longest+1,
                  media_dt_min_ms=float(differences.min()) if len(differences) else None,
                  media_dt_max_ms=float(differences.max()) if len(differences) else None,
                  contact_sheet=str((output/f'{source.stem}-contact.jpg').resolve()),
                  capture_timestamps_available=False,
                  note="Container timestamps do not establish DXGI capture cadence or dropped source frames.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    sources = sorted(args.directory.glob('*.avi'))
    if not sources:
        raise ValueError('No AVI recordings found')
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for source in sources:
        result = inspect(source, args.output)
        results.append(result)
        print(json.dumps(result), flush=True)
    (args.output/'manifest.json').write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
