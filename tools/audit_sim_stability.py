"""Recompute schema-3 stability evidence from complete ordered frame logs."""
import argparse
import json
import math
from pathlib import Path


def audit(summary, rows):
    if summary.get('schema_version') != 3:
        raise ValueError('Requires schema 3; historical results are not maintenance evidence')
    groups = {}
    for row in rows:
        groups.setdefault(row['scene'], []).append(row)
    scenarios = summary['scenarios']
    if len({s['id'] for s in scenarios}) != len(scenarios) or set(groups) != {s['id'] for s in scenarios}:
        raise ValueError('Missing, duplicate or extra scenes')
    verified = {}
    for scene in scenarios:
        frames = groups[scene['id']]
        if [r['frame'] for r in frames] != list(range(scene['planned_frames'])):
            raise ValueError('Incomplete or reordered frames')
        for r in frames:
            if not math.isfinite(r['error_norm']) or r['error_norm'] < 0 or type(r['control_valid']) is not bool:
                raise ValueError('Invalid frame evidence')
            if r['control_valid'] != (r['control_gate'] == 'ready'):
                raise ValueError('Control gate disagrees with validity')
        suffix = 0
        for r in reversed(frames):
            if r['frame'] < scene['success_after_frame'] or not r['control_valid'] or r['error_norm'] >= scene['epsilon_enter']:
                break
            suffix += 1
        after = frames[scene['terminal_frame']+1:] if scene['result'] == 'SUCCESS' else []
        bad = [r['frame'] for r in after if not r['control_valid'] or r['error_norm'] > scene['epsilon_exit']]
        expected = dict(end_stable_frames=suffix, end_stable=suffix >= scene['settle_frames'],
                        post_success_frames=len(after), post_success_violations=len(bad),
                        first_post_success_violation=bad[0] if bad else -1,
                        success_maintained=scene['result'] == 'SUCCESS' and len(after) >= scene['settle_frames'] and not bad)
        if any(scene['metrics'][k] != v for k, v in expected.items()):
            raise ValueError(f'Stability metrics disagree: {scene["id"]}')
        verified[scene['id']] = expected
    totals = {k: sum(v[k] for v in verified.values()) for k in ('success_maintained', 'end_stable')}
    if any(summary['aggregate'][k] != v for k, v in totals.items()):
        raise ValueError('Aggregate disagrees')
    return dict(status='verified', scenarios=verified, aggregate=totals,
                scope='stability_accounting_not_overall_acceptance')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    summary = json.loads((args.run/'summary.json').read_text(encoding='utf-8'))
    rows = [json.loads(line) for line in (args.run/'runs.jsonl').read_text(encoding='utf-8').splitlines()]
    report = audit(summary, rows)
    (args.run/'stability-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report['aggregate']))
