"""Verify paired CV/IMM recordings; report observations, never identity accuracy."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path

import cv2


def verify_run(directory, expected_frames, imm):
    summary = json.loads((directory/'summary.json').read_text())
    assert summary['status'] == 'ok'
    assert summary['frames'] == summary['captured'] == summary['processed'] == expected_frames
    assert all(summary[k] == 0 for k in ('replaced','rejected','pending','stale','failed'))
    rows = [json.loads(line) for line in (directory/'frames.jsonl').read_text().splitlines()]
    assert len(rows) == expected_frames
    ids, confirmed_ids = set(), set()
    states, time_sources = Counter(), Counter()
    for index, row in enumerate(rows):
        assert row['frame_id'] == index
        assert index == 0 or row['source_ts_ns'] > rows[index-1]['source_ts_ns']
        time_sources[row['timestamp_source']] += 1
        for track in row['tracks']:
            ids.add(track['id'])
            states[track['state']] += 1
            if track['state'] == 'Confirmed':
                confirmed_ids.add(track['id'])
            assert ('mode_probabilities' in track) == imm
            if imm:
                p = track['mode_probabilities']
                assert len(p) == 3 and all(math.isfinite(v) and 0 <= v <= 1 for v in p)
                assert abs(sum(p)-1) < 1e-5
    cap = cv2.VideoCapture(str(directory/'overlay.mp4'))
    decoded = 0
    while cap.read()[0]:
        decoded += 1
    cap.release()
    assert decoded == expected_frames
    stats = dict(frames=expected_frames, overlay_decoded_frames=decoded,
                 timestamp_sources=dict(time_sources), automatic_ids=len(ids),
                 automatic_confirmed_ids=len(confirmed_ids), track_state_records=dict(states),
                 frames_with_person_detection=sum(any(d['class_id']==1 for d in r['detections']) for r in rows),
                 frames_without_any_detection=sum(not r['detections'] for r in rows),
                 frames_with_confirmed_track=sum(any(t['state']=='Confirmed' for t in r['tracks']) for r in rows),
                 frames_with_ambiguous_track=sum(any(t['identity_ambiguous'] for t in r['tracks']) for r in rows),
                 mot_ms=summary['mot_ms'])
    return rows, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('replay',type=Path)
    parser.add_argument('output',type=Path)
    args = parser.parse_args()
    records = json.loads(args.manifest.read_text())
    report = dict(ground_truth_available=False,
                  note='Automatic ID counts are not ID switches; detection occupancy is not recall.',
                  recordings=[])
    for record in records:
        stem = Path(record['source']).stem
        cv_rows, cv_stats = verify_run(args.replay/f'{stem}-cv',record['decoded_frames'],False)
        imm_rows, imm_stats = verify_run(args.replay/f'{stem}-imm',record['decoded_frames'],True)
        for a,b in zip(cv_rows, imm_rows):
            for key in ('frame_id','source_ts_ns','timestamp_source','detections'):
                assert a[key] == b[key], (stem, a['frame_id'], key)
        result = dict(video=stem, cv=cv_stats, imm=imm_stats, same_detections=True)
        report['recordings'].append(result)
        print(json.dumps(result),flush=True)
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(report,stream,indent=2)


if __name__ == '__main__':
    main()
