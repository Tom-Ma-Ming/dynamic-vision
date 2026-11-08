"""Fixed-sample diagnostic image-loop suite; never a representative test set."""
import argparse
import hashlib
import json
import math
import statistics
import subprocess
from pathlib import Path

import cv2
import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rectangle(shape, width, height):
    points = np.asarray(shape['points'], dtype=float)
    if shape.get('shape_type') != 'rectangle' or points.shape not in ((2, 2), (4, 2)) or not np.isfinite(points).all():
        raise ValueError('Expected finite rectangle')
    lo = np.maximum(0, np.floor(points.min(axis=0))).astype(int)
    hi = np.minimum([width, height], np.ceil(points.max(axis=0))).astype(int)
    if np.any(hi <= lo):
        raise ValueError('Empty rectangle')
    return [int(lo[0]), int(lo[1]), int(hi[0]), int(hi[1])]


def read_rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def audit_run(folder):
    feedback = read_rows(folder/'feedback.jsonl')
    detections = read_rows(folder/'detections.jsonl')
    if len(feedback) != 100 or len(detections) != 100:
        raise ValueError('Incomplete run')
    signed_x, signed_y, estimate_errors = [], [], []
    # At most one highest-score person per frame; no truth-based detection selection.
    for i, (f, d) in enumerate(zip(feedback, detections)):
        if f['frame'] != i or d['frame'] != i:
            raise ValueError('Frame alignment failure')
        people = [b for b in d['detections'] if b['class_id'] == 1]
        if people:
            b = max(people, key=lambda b: b['score'])['xyxy']
            signed_x.append((b[0]+b[2])/2-f['truth_x'])
            signed_y.append((b[1]+b[3])/2-f['truth_y'])
        if f['track_id']:
            estimate_errors.append(math.hypot(f['est_x']-f['truth_x'], f['est_y']-f['truth_y']))
    cap = cv2.VideoCapture(str(folder/'scene.avi'))
    decoded = 0
    while True:
        ok, _ = cap.read()
        if not ok:
            break
        decoded += 1
    cap.release()
    if decoded != 100:
        raise ValueError('Incomplete output video')
    mean = lambda values: statistics.mean(values) if values else None
    return dict(decoded=decoded, person_frames=len(signed_x),
                person_center_bias_x=mean(signed_x), person_center_bias_y=mean(signed_y),
                tracked_estimate_mae=mean(estimate_errors), final_error_px=feedback[-1]['error_px'],
                summary=json.loads((folder/'summary.json').read_text()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('labels', 'executable', 'engine', 'tracker', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(scope='diagnostic_crop_composites_not_acceptance',
                  sampling='clip01/clip02 indices 15,45,75; largest annotated person; empty samples recorded',
                  hashes={n: digest(getattr(args, n)) for n in ('executable', 'engine', 'tracker')}, cases=[])
    for clip in ('clip01', 'clip02'):
        annotations = sorted(p for p in (args.labels/clip).glob('*.json') if p.name != 'manifest.json')
        for index in (15, 45, 75):
            source = annotations[index]
            annotation = json.loads(source.read_text(encoding='utf-8-sig'))
            image = cv2.imread(str(source.with_suffix('.png')))
            if image is None:
                raise ValueError(f'Missing image: {source}')
            boxes = [rectangle(s, image.shape[1], image.shape[0]) for s in annotation['shapes']
                     if s['label'] == 'person']
            case = dict(id=f'{clip}-{index}', source=str(source.resolve()),
                        annotation_sha256=digest(source), image_sha256=digest(source.with_suffix('.png')))
            report['cases'].append(case)
            if not boxes:
                case['status'] = 'no_annotated_person'
                continue
            x0, y0, x1, y1 = max(boxes, key=lambda b: (b[2]-b[0])*(b[3]-b[1]))
            asset = args.output/case['id']; asset.mkdir()
            cv2.imwrite(str(asset/'sprite.png'), image[y0:y1, x0:x1])
            cv2.imwrite(str(asset/'background.png'), np.full((320, 320, 3), 80, np.uint8))
            case['rectangle'] = [x0, y0, x1, y1]
            cmd = [str(args.executable.resolve()), str(args.engine.resolve()), str((asset/'sprite.png').resolve()),
                   str((asset/'background.png').resolve()), str(args.tracker.resolve()), str((asset/'run').resolve())]
            result = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=120)
            (asset/'process.log').write_text(result.stdout+result.stderr, encoding='utf-8')
            if result.returncode:
                raise RuntimeError(f'{case["id"]} exit {result.returncode}')
            case.update(status='completed', evidence=audit_run(asset/'run'))
    (args.output/'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    for case in report['cases']:
        print(json.dumps(case, ensure_ascii=False))


if __name__ == '__main__':
    main()
