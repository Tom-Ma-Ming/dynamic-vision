"""Repeat decoded video for single-process resource testing, never identity scoring."""
import argparse
import hashlib
import json
from pathlib import Path
import cv2


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path, help='New output directory')
    parser.add_argument('--repeats', type=int, default=20)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('repeats must be positive')
    args.output.mkdir(parents=True, exist_ok=False)
    original_hash = digest(args.source)
    capture = cv2.VideoCapture(str(args.source))
    fps = capture.get(cv2.CAP_PROP_FPS)
    size = (int(capture.get(cv2.CAP_PROP_FRAME_WIDTH)), int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT)))
    if not capture.isOpened() or not 0 < fps < 1000 or min(size) <= 0:
        raise ValueError('Invalid source video')
    output = args.output / 'repeated.avi'
    writer = cv2.VideoWriter(str(output), cv2.VideoWriter_fourcc(*'MJPG'), fps, size)
    counts = []
    try:
        if not writer.isOpened():
            raise ValueError('Cannot open video writer')
        for repeat in range(args.repeats):
            if repeat:
                capture.release()
                capture = cv2.VideoCapture(str(args.source))
            count = 0
            while True:
                ok, frame = capture.read()
                if not ok:
                    break
                if frame.shape[1::-1] != size:
                    raise ValueError('Source shape changed')
                writer.write(frame)
                count += 1
            if not count or (counts and count != counts[0]):
                raise ValueError('Source decoding length changed')
            counts.append(count)
            print(f'repeat {repeat+1}/{args.repeats}: {count} frames', flush=True)
    finally:
        capture.release()
        writer.release()
    if digest(args.source) != original_hash:
        raise ValueError('Source changed during fixture creation')
    manifest = dict(purpose='resource_soak_only_not_tracking_accuracy',
                    source=str(args.source.resolve()), source_sha256=original_hash,
                    repeats=args.repeats, frames_per_repeat=counts[0], frames=sum(counts),
                    fps=fps, size=size, duration_s=sum(counts)/fps,
                    codec='MJPG_reencoded', seam_frames=[i*counts[0] for i in range(1,args.repeats)],
                    output_sha256=digest(output))
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
