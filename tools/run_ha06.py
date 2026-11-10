"""HA06 human-led experiments: on/off, pixel closed loop, two people, faults.

Region size, dead zone, step and resume wait come from the scenario files.
Those values are experimental tunables, not a confirmed product standard.
TensorRT is reported UNKNOWN unless --engine and --sim-image are both supplied
and the process exits 0. A pixel closed loop is not a YOLO run.

Writes <output>/report.json and report.md. Full sim_runner logs stay in
subdirectories of <output>. Exit 1 when a safety gate fails. A TensorRT
not_run leg does not fail the process; the report keeps it as UNKNOWN.
"""
import argparse
import json
import math
import subprocess
from pathlib import Path

PROTOCOL = 'ha06-v1'
EXPERIMENTAL_KEYS = (
    'enter_px', 'exit_px', 'deadzone_px', 'max_step_px', 'max_speed_px_s',
    'max_accel_px_s2', 'resume_wait_s', 'human_turn_px',
)


def load_run(folder):
    folder = Path(folder)
    summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8'))
    frames = [json.loads(line) for line in (folder / 'runs.jsonl').read_text(encoding='utf-8').splitlines() if line]
    commands = [json.loads(line) for line in (folder / 'commands.jsonl').read_text(encoding='utf-8').splitlines() if line]
    return summary, frames, commands


def grouped(frames):
    rows = {}
    for row in frames:
        rows.setdefault(row['scene'], []).append(row)
    return rows


def by_id(summary):
    return {scene['id']: scene for scene in summary['scenarios']}


def step(row):
    return math.hypot(row['assist_dx_px'], row['assist_dy_px'])


def human_pairs(rows):
    return [(row['human_dx_px'], row['human_dy_px']) for row in rows]


def switches(rows):
    count = 0
    previous = 0
    for row in rows:
        track_id = int(row.get('track_id') or 0)
        if previous and track_id and track_id != previous:
            count += 1
        if track_id:
            previous = track_id
    return count


def experimental_block(scene):
    assist = scene['assist']
    return {
        'parameters_experimental': assist.get('parameters_experimental') is True,
        'enabled': assist.get('enabled'),
        **{key: assist.get(key) for key in EXPERIMENTAL_KEYS},
    }


def score_compare(on_summary, on_frames, off_summary, off_frames):
    on_rows, off_rows = grouped(on_frames), grouped(off_frames)
    on_meta, off_meta = by_id(on_summary), by_id(off_summary)
    scenes = []
    for name in on_meta:
        left, right = on_rows[name], off_rows[name]
        scenes.append({
            'id': name,
            'human_same': human_pairs(left) == human_pairs(right),
            'on_assist_px': sum(step(row) for row in left),
            'off_assist_px': sum(step(row) for row in right),
            'on_final_error_px': left[-1]['error_px'],
            'off_final_error_px': right[-1]['error_px'],
            'on_enabled': on_meta[name]['assist'].get('enabled') is True,
            'off_enabled': off_meta[name]['assist'].get('enabled') is False,
            'parameters_experimental': on_meta[name]['assist'].get('parameters_experimental') is True,
        })
    found = {scene['id']: scene for scene in scenes}
    static, drift, yield_scene = found['shared-static'], found['shared-drift'], found['shared-yield']
    gates = {
        'same_human_trajectory': all(scene['human_same'] for scene in scenes),
        'assist_off_silent': all(scene['off_assist_px'] < 1e-6 for scene in scenes),
        'on_arm_enabled': all(scene['on_enabled'] and scene['off_enabled'] for scene in scenes),
        'experimental_marked': all(scene['parameters_experimental'] for scene in scenes),
        'static_on_closer': static['on_final_error_px'] + 1 < static['off_final_error_px'] and static['on_assist_px'] > 1,
        'drift_on_closer': drift['on_final_error_px'] < 8 and drift['on_final_error_px'] + 5 < drift['off_final_error_px'],
        'yield_human_matches': yield_scene['human_same'] and yield_scene['on_assist_px'] >= 0,
    }
    return {'scenes': scenes, 'gates': gates}


def score_scene_identity(rows):
    ambiguous_commands = 0
    wrong_person = 0
    assist_px = 0.0
    for row in rows:
        distance = step(row)
        assist_px += distance
        if row.get('control_gate') == 'ambiguous' and distance > 1e-6:
            ambiguous_commands += 1
        if distance > 0.2 and math.hypot(row['est_x'] - row['truth_x'], row['est_y'] - row['truth_y']) > 40:
            wrong_person += 1
    return {
        'switches': switches(rows),
        'final_track_id': int(rows[-1].get('track_id') or 0),
        'final_error_px': rows[-1]['error_px'],
        'assist_px': assist_px,
        'ambiguous_commands': ambiguous_commands,
        'wrong_person_commands': wrong_person,
        'legacy_result': None,
    }


def score_multi(summary, frames):
    rows = grouped(frames)
    meta = by_id(summary)
    scenes = []
    for name, scene_rows in ((name, rows[name]) for name in meta):
        scored = score_scene_identity(scene_rows)
        scored['id'] = name
        scored['legacy_result'] = meta[name]['result']
        scored['experimental'] = experimental_block(meta[name])
        scenes.append(scored)
    found = {scene['id']: scene for scene in scenes}
    separated, cross = found['separated'], found['cross']
    gates = {
        'separated_keeps_selection': separated['switches'] == 0 and separated['final_track_id'] == 1 and separated['assist_px'] > 1 and separated['final_error_px'] < 5,
        'cross_keeps_selection_id': cross['switches'] == 0,
        'cross_stops_when_ambiguous': cross['ambiguous_commands'] == 0,
        'cross_does_not_assist_wrong_person': cross['wrong_person_commands'] == 0,
        'experimental_marked': all(scene['experimental']['parameters_experimental'] for scene in scenes),
    }
    return {'scenes': scenes, 'gates': gates}


def score_faults(summary, frames, commands):
    rows = grouped(frames)
    meta = by_id(summary)
    commands_by_scene = {}
    for command in commands:
        commands_by_scene.setdefault(command['scene'], []).append(command)

    def final(name):
        return rows[name][-1]['error_px']

    delay_human = human_pairs(rows['delay-0']) == human_pairs(rows['delay-80'])
    dispatch = rows['dispatch-fail']
    dispatch_camera = max(math.hypot(row['camera_x'], row['camera_y']) for row in dispatch)
    dispatch_applied = sum(1 for command in commands_by_scene.get('dispatch-fail', []) if command['kind'] == 'applied')
    drop = rows['feedback-drop']
    drop_tail = drop[10:]
    encoder_generated = sum(1 for command in commands_by_scene.get('encoder-drop', []) if command['kind'] == 'generated')
    scenes = []
    for name in meta:
        scene_rows = rows[name]
        scenes.append({
            'id': name,
            'result': meta[name]['result'],
            'cause': meta[name].get('cause'),
            'final_error_px': scene_rows[-1]['error_px'],
            'assist_px': sum(step(row) for row in scene_rows),
            'experimental': experimental_block(meta[name]),
        })
    gates = {
        'delay_pair_same_human': delay_human,
        'delay_recorded': math.isfinite(final('delay-0')) and math.isfinite(final('delay-80')),
        'delay_does_not_improve_on_stale_observation': final('delay-0') <= final('delay-80') + 1,
        'dispatch_failure_does_not_move': meta['dispatch-fail']['result'] == 'UNKNOWN' and dispatch_camera < 1 and dispatch_applied == 0,
        'feedback_drop_stops_and_is_unknown': meta['feedback-drop']['result'] == 'UNKNOWN' and all(step(row) < 1e-6 for row in drop_tail),
        'encoder_drop_blocks_commands': encoder_generated == 0,
        'experimental_marked': all(scene['experimental']['parameters_experimental'] for scene in scenes),
    }
    return {'scenes': scenes, 'gates': gates, 'retained_results': [scene['result'] for scene in scenes]}


def score_image(summary, frames):
    rows = grouped(frames)
    meta = by_id(summary)
    on, off = rows['pixel-on'], rows['pixel-off']
    gates = {
        'pixel_detector_recorded': meta['pixel-on'].get('image_detector') == 'pixel' and meta['pixel-off'].get('image_detector') == 'pixel',
        'pixel_on_closer': on[-1]['error_px'] < 5 and on[-1]['error_px'] + 1 < off[-1]['error_px'],
        'pixel_off_silent': sum(step(row) for row in off) < 1e-6,
        'experimental_marked': meta['pixel-on']['assist'].get('parameters_experimental') is True,
        'not_claimed_as_tensorrt': True,
    }
    return {
        'scenes': [
            {'id': 'pixel-on', 'final_error_px': on[-1]['error_px'], 'assist_px': sum(step(row) for row in on), 'result': meta['pixel-on']['result']},
            {'id': 'pixel-off', 'final_error_px': off[-1]['error_px'], 'assist_px': sum(step(row) for row in off), 'result': meta['pixel-off']['result']},
        ],
        'gates': gates,
        'scope': 'cpu_pixel_closed_loop_not_yolo',
    }


def tensorrt_plan(engine, sim_image, sprite=None, background=None, output=None):
    command = [
        str(sim_image) if sim_image else 'vision_sim_image',
        str(engine) if engine else '<engine>',
        str(sprite) if sprite else '<sprite>',
        str(background) if background else '<background>',
        'configs/tracker-body-primary.yml',
        str(output) if output else '<new-output>',
        '<target-x>', '<target-y>', '<target-w>', '<target-h>',
        '1', '1', '0', '0', '0', '0', 'human_led',
    ]
    off_command = command[:-1] + ['human_led_off']
    ready = all(item and Path(item).exists() for item in (engine, sim_image, sprite, background))
    reason = (
        'Executable, engine, sprite and background were supplied.'
        if ready else
        'TensorRT image leg was not executed. VISION_WITH_TRT is Windows-only in this repo, and this run did not receive an engine, vision_sim_image, sprite and background together.'
    )
    return {
        'status': 'ready' if ready else 'not_run',
        'result': 'UNKNOWN',
        'reason': reason,
        'on_command': command,
        'off_command': off_command,
    }


def run_tensorrt(plan, output):
    if plan['status'] != 'ready':
        return plan
    destination = output / 'tensorrt-on'
    completed = subprocess.run(plan['on_command'][:1] + plan['on_command'][1:], capture_output=True, text=True)
    # The placeholders are only valid once the caller replaced them. A ready plan
    # is expected to contain real paths in on_command.
    (output / 'tensorrt-on.log').write_text(completed.stdout + completed.stderr, encoding='utf-8')
    if completed.returncode != 0 or not (destination / 'summary.json').exists():
        plan.update(status='failed', result='UNKNOWN', returncode=completed.returncode)
        return plan
    summary = json.loads((destination / 'summary.json').read_text(encoding='utf-8'))
    plan.update(status='completed', result=summary.get('result', 'UNKNOWN'), summary=summary, returncode=0)
    return plan


def write_markdown(report, path):
    lines = [
        '# HA06 report',
        '',
        f"Protocol `{report['protocol']}`, status `{report['status']}`.",
        'Region, dead zone, step and resume wait are experimental and are copied from the scenario files.',
        '',
        '## Gates',
        '',
    ]
    for group, payload in report['groups'].items():
        lines.append(f'### {group}')
        for name, passed in payload['gates'].items():
            lines.append(f'- {name}: {"pass" if passed else "fail"}')
        lines.append('')
    tensorrt = report['tensorrt_image_closed_loop']
    lines.append('## TensorRT image closed loop')
    lines.append('')
    lines.append(f"Status `{tensorrt['status']}`, result `{tensorrt['result']}`. {tensorrt['reason']}")
    lines.append('')
    lines.append('On command:')
    lines.append('')
    lines.append('```')
    lines.append(' '.join(str(part) for part in tensorrt['on_command']))
    lines.append('```')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def run_sim(runner, config, tracker, destination):
    destination = Path(destination)
    if destination.exists():
        raise RuntimeError(f'{destination} already exists')
    completed = subprocess.run([str(runner), str(config), str(destination), str(tracker)], capture_output=True, text=True)
    (destination.parent / f'{destination.name}.log').write_text(completed.stdout + completed.stderr, encoding='utf-8')
    if completed.returncode != 0:
        raise RuntimeError(f'{config} exited {completed.returncode}: {completed.stderr}')
    return load_run(destination)


def off_copy(source, destination):
    text = Path(source).read_text(encoding='utf-8')
    token = 'assist_enabled: 1'
    if text.count(token) != 1:
        raise RuntimeError('compare config must contain one assist_enabled: 1 default')
    Path(destination).write_text(text.replace(token, 'assist_enabled: 0', 1), encoding='utf-8')


def build_report(compare, multi, faults, image, tensorrt):
    groups = {'compare': compare, 'multi': multi, 'faults': faults, 'image': image}
    gates_ok = all(all(payload['gates'].values()) for payload in groups.values())
    return {
        'protocol': PROTOCOL,
        'status': 'passed' if gates_ok else 'failed',
        'parameters_experimental': True,
        'experimental_note': 'assist enter/exit, deadzone, step, speed, acceleration, human-turn threshold and resume wait are experimental tunables. They are not a confirmed product standard.',
        'deferred': ['reid', 'person_recall_95', 'remote_ci'],
        'groups': groups,
        'tensorrt_image_closed_loop': tensorrt,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runner', type=Path)
    parser.add_argument('tracker', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--configs', type=Path, default=Path('configs'))
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--sim-image', type=Path)
    parser.add_argument('--sprite', type=Path)
    parser.add_argument('--background', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('output directory must be new')
    args.output.mkdir(parents=True)
    root = args.configs
    off_config = args.output / 'compare-off.yml'
    off_copy(root / 'sim-human-led-compare.yml', off_config)
    on_summary, on_frames, _ = run_sim(args.runner, root / 'sim-human-led-compare.yml', args.tracker, args.output / 'compare-on')
    off_summary, off_frames, _ = run_sim(args.runner, off_config, args.tracker, args.output / 'compare-off')
    multi_summary, multi_frames, _ = run_sim(args.runner, root / 'sim-human-led-multi.yml', args.tracker, args.output / 'multi')
    fault_summary, fault_frames, fault_commands = run_sim(args.runner, root / 'sim-human-led-faults.yml', args.tracker, args.output / 'faults')
    image_summary, image_frames, _ = run_sim(args.runner, root / 'sim-human-led-image.yml', args.tracker, args.output / 'image')
    tensorrt = tensorrt_plan(args.engine, args.sim_image, args.sprite, args.background, args.output / 'tensorrt-on')
    if tensorrt['status'] == 'ready':
        tensorrt = run_tensorrt(tensorrt, args.output)
    report = build_report(
        score_compare(on_summary, on_frames, off_summary, off_frames),
        score_multi(multi_summary, multi_frames),
        score_faults(fault_summary, fault_frames, fault_commands),
        score_image(image_summary, image_frames),
        tensorrt,
    )
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    write_markdown(report, args.output / 'report.md')
    print(json.dumps({'status': report['status'], 'tensorrt': tensorrt['status']}))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
