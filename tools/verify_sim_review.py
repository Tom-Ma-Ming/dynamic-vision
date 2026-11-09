"""Run fixed independent vertical-control and stage-clock regression cases.

Exit 1 means a review gate failed, not that the subprocess failed to execute.
These necessary checks do not establish full control accuracy or causality.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
from audit_sim_stability import audit as audit_stability
from audit_sim_commands import audit as audit_commands


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load_run(root):
    summary = json.loads((root/'summary.json').read_text(encoding='utf-8'))
    frames = [json.loads(s) for s in (root/'runs.jsonl').read_text(encoding='utf-8').splitlines()]
    commands = [json.loads(s) for s in (root/'commands.jsonl').read_text(encoding='utf-8').splitlines()]
    audit_stability(summary, frames)
    audit_commands(commands)
    names = {s['id'] for s in summary['scenarios']}
    if {f['scene'] for f in frames} != names:
        raise ValueError('Frame scenes disagree with summary')
    for scene in summary['scenarios']:
        rows = [f for f in frames if f['scene'] == scene['id']]
        if [r['frame'] for r in rows] != list(range(scene['planned_frames'])):
            raise ValueError('Missing or unordered frames')
        if not all(math.isfinite(r[k]) for r in rows for k in ('truth_x', 'truth_y', 'camera_x', 'camera_y', 'error_px')):
            raise ValueError('Nonfinite frame evidence')
    return summary, frames, commands


def verify(output):
    summary, frames, commands = load_run(output/'vertical')
    scenarios = {s['id']: s for s in summary['scenarios']}
    expected = {f'{kind}-comp{mode}' for kind in ('vertical_offset', 'vertical_motion') for mode in (0, 1)}
    if set(scenarios) != expected:
        raise ValueError('Unexpected vertical scenarios')
    checks, details = {}, {}
    for name, scene in scenarios.items():
        rows = [f for f in frames if f['scene'] == name]
        sent = [c for c in commands if c['scene'] == name and c['kind'] == 'generated']
        nonzero = sum(abs(c['dy']) > 1e-9 for c in sent)
        checks[name+'_axis_active'] = nonzero > 0
        if name.startswith('vertical_offset'):
            checks[name+'_end_stable'] = scene['metrics']['end_stable'] is True
        details[name] = dict(nonzero_y_commands=nonzero, final_error_px=rows[-1]['error_px'])
    _, frames, _ = load_run(output/'async')
    if {f['scene'] for f in frames} != {'async0', 'async1'}:
        raise ValueError('Unexpected clock scenarios')
    for name in ('async0', 'async1'):
        rows = [f for f in frames if f['scene'] == name]
        # Fixtures have a stationary world target (120,240); both logged
        # components must refer to the same timestamp to reconstruct it.
        residuals = [math.hypot(f['truth_x']+f['camera_x']-120,
                                f['truth_y']+f['camera_y']-240) for f in rows]
        checks[name+'_coordinate_time_consistent'] = max(residuals) <= .001
        details[name] = dict(max_coordinate_residual_px=max(residuals),
                             inconsistent_frames=sum(x > .001 for x in residuals))
    return dict(status='passed' if all(checks.values()) else 'failed', gates=checks, details=details,
                scope='necessary_regressions_not_full_project_acceptance')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    exe, output = args.exe.resolve(), args.output.resolve()
    sources = [exe, repo/'configs/tracker.yml', repo/'configs/sim-review-vertical.yml', repo/'configs/sim-review-async.yml']
    before = {str(p): sha(p) for p in sources}
    output.mkdir(exist_ok=False, parents=True)
    (output/'manifest.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
    for name, config in [('vertical', sources[2]), ('async', sources[3])]:
        with (output/f'{name}-stdout.log').open('w', encoding='utf-8') as stdout, (output/f'{name}-stderr.log').open('w', encoding='utf-8') as stderr:
            subprocess.run([str(exe), str(config), str(output/name), str(sources[1])],
                           check=True, stdout=stdout, stderr=stderr, timeout=120)
    if before != {str(p): sha(p) for p in sources}:
        raise ValueError('Program or configuration changed during review')
    result = verify(output)
    (output/'verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
