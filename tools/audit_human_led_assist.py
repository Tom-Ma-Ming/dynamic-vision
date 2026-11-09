"""Independent human-led assist score. Eligibility is recomputed here.

The controller's assist_state is not the denominator. A run cannot pass by
leaving assist at zero while eligible frames still need a correction. Outside
the region, zero output is a boundary pass. A demand frame counts as covered
while a correcting command is still inside its generated-to-expires lifetime.
Residual displacement is executed motion during an explicit hold, not motion
after a later release. Limits below are experimental and are not a confirmed
product standard. Stable fraction is the share of eligible frames whose center
error is within the configured deadzone. Jitter is a sign reversal of a
nonzero assist step. Overshoot is the largest center error after the estimate
first crosses the explicit center.
"""
import argparse
import json
import math
from pathlib import Path

PROTOCOL = 'human-led-audit-v1'
# Written before the HA05 run from the already observed 1 px / 1.4 px trims.
EXPERIMENTAL_END_ERROR_PX = 2.5
STEP_EPS = 1e-6


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return None
    index = min(len(ordered) - 1, max(0, math.ceil(fraction * len(ordered)) - 1))
    return ordered[index]


def finite(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


def audit(summary, frames, commands):
    if summary.get('schema_version') != 3:
        raise ValueError('Requires schema 3')
    grouped, grouped_commands = {}, {}
    for row in frames:
        grouped.setdefault(row['scene'], []).append(row)
    for row in commands:
        grouped_commands.setdefault(row['scene'], []).append(row)
    names = [scene['id'] for scene in summary['scenarios']]
    if len(set(names)) != len(names) or set(grouped) != set(names):
        raise ValueError('Missing, duplicate or extra scenes')
    scenes = []
    for scene in summary['scenarios']:
        rows = grouped[scene['id']]
        if [row['frame'] for row in rows] != list(range(scene['planned_frames'])):
            raise ValueError(f'Incomplete frames: {scene["id"]}')
        scenes.append(score_scene(scene, rows, grouped_commands.get(scene['id'], [])))
    demand = sum(scene['demand_frames'] for scene in scenes)
    assisted = sum(scene['assisted_demand_frames'] for scene in scenes)
    gates = {
        'eligible_demand_was_assisted': demand > 0 and assisted > 0,
        'no_out_of_region_output': all(scene['out_of_region_outputs'] == 0 for scene in scenes),
        'no_command_during_hold': all(scene['commands_during_hold'] == 0 for scene in scenes),
        'within_step_limit': all(scene['over_limit_outputs'] == 0 for scene in scenes),
        'no_identity_switch': all(scene['identity_switches'] == 0 for scene in scenes),
        'clock_monotonic': all(scene['clock_monotonic'] for scene in scenes),
        'effect_scenes_settle': all(scene['effect_settled'] for scene in scenes if scene['role'] == 'effect'),
        'effect_direction': all(scene['direction_ok'] for scene in scenes if scene['role'] == 'effect'),
        'hold_residual_bounded': all(scene['residual_ok'] for scene in scenes if scene['role'] == 'hold'),
    }
    return {
        'protocol': PROTOCOL,
        'parameters_experimental': True,
        'experimental_end_error_px': EXPERIMENTAL_END_ERROR_PX,
        'status': 'passed' if all(gates.values()) else 'failed',
        'gates': gates,
        'aggregate': {
            'eligible_time_s': sum(scene['eligible_time_s'] for scene in scenes),
            'eligible_frames': sum(scene['eligible_frames'] for scene in scenes),
            'demand_frames': demand,
            'assisted_demand_frames': assisted,
            'coverage': (assisted / demand) if demand else 0.0,
            'identity_switches': sum(scene['identity_switches'] for scene in scenes),
            'over_limit_outputs': sum(scene['over_limit_outputs'] for scene in scenes),
            'out_of_region_outputs': sum(scene['out_of_region_outputs'] for scene in scenes),
        },
        'scenes': scenes,
        'scope': 'human_led_assist_not_legacy_chase_acceptance',
    }


def score_scene(scene, rows, commands):
    assist = scene['assist']
    if assist.get('mode') != 'human_led' or assist.get('protocol') != 'human-led-v1':
        raise ValueError(f'{scene["id"]} is not a human-led scene')
    center_x, center_y = assist['center_x'], assist['center_y']
    enter, exit_px = assist['enter_px'], assist['exit_px']
    deadzone, max_step = assist['deadzone_px'], assist['max_step_px']
    resume_ns = int(round(assist['resume_wait_s'] * 1e9))
    if not all(finite(value) for value in (center_x, center_y, enter, exit_px, deadzone, max_step)):
        raise ValueError(f'{scene["id"]} has a nonfinite assist contract')
    latched = False
    hold = False
    resume_at = None
    previous_id = 0
    previous_ts = None
    eligible_errors = []
    eligible_offsets = []
    eligible_steps = []
    demand_frames = assisted_demand = eligible_frames = 0
    out_of_region = over_limit = commands_during_hold = identity_switches = 0
    clock_monotonic = True
    assist_dx = assist_dy = 0.0
    residual = 0.0
    hold_started = None
    first_ready = None
    for row in rows:
        if previous_ts is not None and row['ts_ns'] <= previous_ts:
            clock_monotonic = False
        previous_ts = row['ts_ns']
        for key in ('assist_dx_px', 'assist_dy_px', 'executed_dx_px', 'executed_dy_px', 'error_px', 'est_x', 'est_y'):
            if not finite(row[key]):
                raise ValueError(f'{scene["id"]} frame {row["frame"]} has a nonfinite value')
        step = math.hypot(row['assist_dx_px'], row['assist_dy_px'])
        if step > max_step + STEP_EPS:
            over_limit += 1
        assist_dx += row['assist_dx_px']
        assist_dy += row['assist_dy_px']
        if row.get('human_explicit'):
            hold = True
            resume_at = None
            latched = False
            if hold_started is None:
                hold_started = row['ts_ns']
        elif row.get('human_release') and hold:
            hold = False
            resume_at = row['ts_ns'] + resume_ns
            latched = False
        elif row.get('human_takeover') and not row.get('human_explicit'):
            resume_at = row['ts_ns'] + resume_ns
            latched = False
        waiting = resume_at is not None and row['ts_ns'] < resume_at
        ready = row['control_gate'] == 'ready' and row.get('has_track') is True
        distance = math.hypot(row['est_x'] - center_x, row['est_y'] - center_y) if ready else None
        if not ready or hold or waiting:
            latched = False
        elif not latched and distance <= enter:
            latched = True
        elif latched and distance > exit_px:
            latched = False
        if ready and first_ready is None:
            first_ready = (row['truth_x'] - center_x, row['truth_y'] - center_y)
        if hold and step > STEP_EPS:
            commands_during_hold += 1
        if hold:
            residual += math.hypot(row['executed_dx_px'], row['executed_dy_px'])
        track_id = int(row.get('track_id') or 0)
        if previous_id and track_id and track_id != previous_id:
            identity_switches += 1
        if track_id:
            previous_id = track_id
        if latched and ready and not hold and not waiting:
            eligible_frames += 1
            eligible_errors.append(row['error_px'])
            eligible_offsets.append((row['est_x'] - center_x, row['est_y'] - center_y))
            eligible_steps.append((row['assist_dx_px'], row['assist_dy_px']))
            if distance > deadzone:
                demand_frames += 1
                if correcting(row, center_x, center_y) or command_in_flight(commands, row, center_x, center_y):
                    assisted_demand += 1
        elif not latched and ready and distance is not None and distance > enter and step > STEP_EPS:
            out_of_region += 1
    for command in commands:
        if previous_ts is not None and command['timestamp'] < 0:
            clock_monotonic = False
    timestamps = [command['timestamp'] for command in commands]
    if timestamps != sorted(timestamps):
        clock_monotonic = False
    initial_dx, initial_dy = first_ready if first_ready is not None else (None, None)
    released = any(row.get('human_release') for row in rows)
    role = 'hold' if hold and not released else 'effect' if demand_frames else 'boundary'
    direction_ok = True
    if role == 'effect' and initial_dx is not None:
        if abs(initial_dx) >= 4 and abs(initial_dy) < 4:
            direction_ok = assist_dx > 1 and abs(assist_dy) < 1
        elif abs(initial_dy) >= 4 and abs(initial_dx) < 4:
            direction_ok = assist_dy > 1 and abs(assist_dx) < 1
        elif abs(initial_dx) >= 4 and abs(initial_dy) >= 4:
            direction_ok = assist_dx > 1 and assist_dy > 1
    effect_settled = role != 'effect' or (rows[-1]['error_px'] <= EXPERIMENTAL_END_ERROR_PX and assisted_demand > 0)
    dt_ns = rows[1]['ts_ns'] - rows[0]['ts_ns'] if len(rows) > 1 else 0
    stable_fraction = None
    jitter_reversals = 0
    overshoot_px = None
    if eligible_errors:
        stable_fraction = sum(error <= deadzone for error in eligible_errors) / len(eligible_errors)
        previous = None
        for dx, dy in eligible_steps:
            if previous is not None and (dx * previous[0] < -STEP_EPS or dy * previous[1] < -STEP_EPS):
                jitter_reversals += 1
            if abs(dx) > STEP_EPS or abs(dy) > STEP_EPS:
                previous = (dx, dy)
        overshoot_px = 0.0
        crossed = False
        initial_dx, initial_dy = first_ready
        for (offset_x, offset_y), error in zip(eligible_offsets, eligible_errors):
            if not crossed and offset_x * initial_dx + offset_y * initial_dy <= 0:
                crossed = True
                continue
            if crossed:
                overshoot_px = max(overshoot_px, error)
    return {
        'id': scene['id'],
        'role': role,
        'eligible_frames': eligible_frames,
        'eligible_time_s': eligible_frames * dt_ns / 1e9,
        'demand_frames': demand_frames,
        'assisted_demand_frames': assisted_demand,
        'coverage': (assisted_demand / demand_frames) if demand_frames else None,
        'error_p50_px': percentile(eligible_errors, 0.50),
        'error_p95_px': percentile(eligible_errors, 0.95),
        'stable_fraction': stable_fraction,
        'jitter_reversals': jitter_reversals,
        'overshoot_px': overshoot_px,
        'final_error_px': rows[-1]['error_px'],
        'exit_state': rows[-1].get('assist_state'),
        'exit_reason': rows[-1].get('assist_reason'),
        'assist_dx_px': assist_dx,
        'assist_dy_px': assist_dy,
        'over_limit_outputs': over_limit,
        'out_of_region_outputs': out_of_region,
        'commands_during_hold': commands_during_hold,
        'identity_switches': identity_switches,
        'takeover_stop_delay_ns': stop_delay(commands, hold_started),
        'residual_px': residual if hold_started is not None else None,
        'residual_ok': hold_started is None or residual <= max_step + 0.5,
        'clock_monotonic': clock_monotonic,
        'direction_ok': direction_ok,
        'effect_settled': effect_settled,
        'legacy_result': scene['result'],
    }


def correcting(row, center_x, center_y):
    same_x = row['assist_dx_px'] * (row['est_x'] - center_x) > 0.2
    same_y = row['assist_dy_px'] * (row['est_y'] - center_y) > 0.2
    return same_x or same_y


def command_in_flight(commands, row, center_x, center_y):
    """A demand frame is covered while a correcting command is still inside its known lifetime."""
    cancelled = {}
    for command in commands:
        if command['kind'] in ('cancelled', 'cancelled_in_transit', 'dispatch_failed'):
            cancelled.setdefault(command['id'], command['timestamp'])
    for command in commands:
        if command['kind'] != 'generated' or not (command['generated'] <= row['ts_ns'] < command['expires']):
            continue
        if cancelled.get(command['id'], row['ts_ns'] + 1) <= row['ts_ns']:
            continue
        same_x = command['dx'] * (row['est_x'] - center_x) > 0.2
        same_y = command['dy'] * (row['est_y'] - center_y) > 0.2
        if same_x or same_y:
            return True
    return False


def stop_delay(commands, takeover_ns):
    if takeover_ns is None:
        return None
    cancelled = [command['timestamp'] - takeover_ns for command in commands
                 if command['kind'] in ('cancelled', 'cancelled_in_transit') and command['timestamp'] >= takeover_ns]
    return min(cancelled) if cancelled else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    summary = json.loads((args.run / 'summary.json').read_text(encoding='utf-8'))
    frames = [json.loads(line) for line in (args.run / 'runs.jsonl').read_text(encoding='utf-8').splitlines()]
    commands = [json.loads(line) for line in (args.run / 'commands.jsonl').read_text(encoding='utf-8').splitlines()]
    report = audit(summary, frames, commands)
    (args.run / 'human-led-audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'status': report['status'], 'aggregate': report['aggregate']}))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
