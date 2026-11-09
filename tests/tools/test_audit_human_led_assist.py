import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('assist_audit', Path(__file__).parents[2]/'tools/audit_human_led_assist.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def scene(scene_id='inside'):
    return {
        'id': scene_id, 'result': 'FAILED', 'planned_frames': 4,
        'assist': {
            'mode': 'human_led', 'protocol': 'human-led-v1', 'center_x': 0, 'center_y': 0,
            'enter_px': 48, 'exit_px': 72, 'deadzone_px': 1, 'max_step_px': 2,
            'resume_wait_s': 0.2,
        },
    }


def frame(index, est_x=20, assist_dx=2, gate='ready', explicit=False, release=False):
    return {
        'scene': 'inside', 'frame': index, 'ts_ns': index * 40000000, 'control_gate': gate,
        'has_track': gate == 'ready', 'track_id': 1 if gate == 'ready' else 0,
        'est_x': est_x, 'est_y': 0, 'truth_x': est_x, 'truth_y': 0, 'error_px': abs(est_x),
        'assist_dx_px': assist_dx, 'assist_dy_px': 0, 'executed_dx_px': assist_dx, 'executed_dy_px': 0,
        'human_explicit': explicit, 'human_release': release, 'human_takeover': explicit,
    }


def test_never_assisting_an_eligible_target_fails():
    rows = [frame(i, assist_dx=0) for i in range(4)]
    for row in rows:
        row['error_px'] = 20
    report = module.audit({'schema_version': 3, 'scenarios': [scene()]}, rows, [])
    assert report['status'] == 'failed'
    assert report['gates']['eligible_demand_was_assisted'] is False


def test_outside_zero_output_is_a_boundary_pass_when_another_scene_assists():
    outside = [frame(i, est_x=200, assist_dx=0) for i in range(4)]
    for row in outside:
        row['scene'] = 'outside'
        row['error_px'] = 200
    inside = [frame(i, est_x=max(1, 20 - i * 6), assist_dx=2 if i < 3 else 0) for i in range(4)]
    inside[-1]['error_px'] = 1
    summary = {'schema_version': 3, 'scenarios': [scene(), scene('outside')]}
    report = module.audit(summary, inside + outside, [])
    assert report['scenes'][1]['role'] == 'boundary'
    assert report['scenes'][1]['out_of_region_outputs'] == 0
    assert report['gates']['no_out_of_region_output'] is True


def test_explicit_hold_rejects_a_later_assist_command():
    rows = [frame(0, assist_dx=2), frame(1, est_x=18, assist_dx=0, explicit=True), frame(2, est_x=18, assist_dx=2), frame(3, est_x=16, assist_dx=0)]
    report = module.audit({'schema_version': 3, 'scenarios': [scene()]}, rows, [])
    assert report['scenes'][0]['commands_during_hold'] == 1
    assert report['gates']['no_command_during_hold'] is False


def test_sign_reversal_is_reported_as_jitter():
    rows = [frame(0, est_x=10, assist_dx=2), frame(1, est_x=4, assist_dx=-2), frame(2, est_x=2, assist_dx=0), frame(3, est_x=1, assist_dx=0)]
    rows[-1]['error_px'] = 1
    report = module.audit({'schema_version': 3, 'scenarios': [scene()]}, rows, [])
    assert report['scenes'][0]['jitter_reversals'] == 1
    assert report['scenes'][0]['eligible_time_s'] == pytest.approx(0.16)


def test_step_above_the_declared_limit_fails():
    rows = [frame(i, assist_dx=3, est_x=max(1, 8 - i * 2)) for i in range(4)]
    rows[-1]['error_px'] = 1
    report = module.audit({'schema_version': 3, 'scenarios': [scene()]}, rows, [])
    assert report['scenes'][0]['over_limit_outputs'] == 4
    assert report['status'] == 'failed'
