import importlib.util
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location('ha06', Path(__file__).parents[2] / 'tools' / 'run_ha06.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def assist(enabled=True):
    return {
        'parameters_experimental': True, 'enabled': enabled,
        'enter_px': 48, 'exit_px': 72, 'deadzone_px': 1, 'max_step_px': 2,
        'max_speed_px_s': 50, 'max_accel_px_s2': 500, 'resume_wait_s': 0.2, 'human_turn_px': 12,
    }


def frame(scene, index, error, assist_dx=0, human_dx=0, gate='ready', track_id=1, est_x=0, truth_x=0):
    return {
        'scene': scene, 'frame': index, 'error_px': error, 'assist_dx_px': assist_dx, 'assist_dy_px': 0,
        'human_dx_px': human_dx, 'human_dy_px': 0, 'control_gate': gate, 'track_id': track_id,
        'est_x': est_x, 'est_y': 0, 'truth_x': truth_x, 'truth_y': 0, 'camera_x': 0, 'camera_y': 0,
    }


def test_compare_requires_the_same_human_samples_and_a_closer_on_arm():
    def pack(scene, errors, assist_dx, human, enabled):
        return [frame(scene, i, error, assist_dx if i == 0 else 0, human if i == 0 else 0) for i, error in enumerate(errors)]
    on_frames = pack('shared-static', [12, 2], 2, 8, True) + pack('shared-drift', [1, 2], 1, 0, True) + pack('shared-yield', [20, 18], 1, 30, True)
    off_frames = pack('shared-static', [12, 12], 0, 8, False) + pack('shared-drift', [1, 30], 0, 0, False) + pack('shared-yield', [20, 40], 0, 30, False)
    summary = lambda enabled: {'scenarios': [
        {'id': name, 'assist': assist(enabled)} for name in ('shared-static', 'shared-drift', 'shared-yield')
    ]}
    report = module.score_compare(summary(True), on_frames, summary(False), off_frames)
    assert report['gates']['same_human_trajectory'] is True
    assert report['gates']['static_on_closer'] is True
    assert report['gates']['drift_on_closer'] is True
    off_frames[0]['human_dx_px'] = 9
    report = module.score_compare(summary(True), on_frames, summary(False), off_frames)
    assert report['gates']['same_human_trajectory'] is False


def test_cross_command_on_the_wrong_body_fails_and_ambiguous_output_fails():
    rows = [
        frame('separated', 0, 2, assist_dx=2, track_id=1, est_x=1, truth_x=1),
        frame('cross', 0, 4, assist_dx=0, gate='ambiguous', track_id=1, est_x=10, truth_x=0),
        frame('cross', 1, 4, assist_dx=2, track_id=1, est_x=80, truth_x=0),
    ]
    summary = {'scenarios': [
        {'id': 'separated', 'result': 'FAILED', 'assist': assist()},
        {'id': 'cross', 'result': 'FAILED', 'assist': assist()},
    ]}
    # separated needs a final low error; give it a second frame via the first only.
    rows[0]['error_px'] = 2
    report = module.score_multi(summary, rows)
    assert report['gates']['cross_stops_when_ambiguous'] is True
    assert report['gates']['cross_does_not_assist_wrong_person'] is False
    rows[1]['assist_dx_px'] = 2
    report = module.score_multi(summary, rows)
    assert report['gates']['cross_stops_when_ambiguous'] is False


def test_fault_report_keeps_unknown_and_rejects_a_moving_failed_dispatch():
    def scene(name, result='UNKNOWN'):
        return {'id': name, 'result': result, 'cause': 'control', 'assist': assist()}
    names = ('delay-0', 'delay-80', 'dispatch-fail', 'feedback-drop', 'encoder-drop')
    frames = []
    for name, error in (('delay-0', 2), ('delay-80', 6)):
        frames.append(frame(name, 0, error, human_dx=8))
    frames.append(frame('dispatch-fail', 0, 20))
    frames += [frame('feedback-drop', i, 20, assist_dx=2 if i < 3 else 0) for i in range(12)]
    frames.append(frame('encoder-drop', 0, 20, assist_dx=0))
    summary = {'scenarios': [scene(name, 'FAILED' if name.startswith('delay') else 'UNKNOWN') for name in names]}
    commands = [{'scene': 'encoder-drop', 'kind': 'generated'}]
    report = module.score_faults(summary, frames, commands)
    assert 'UNKNOWN' in report['retained_results']
    assert 'FAILED' in report['retained_results']
    assert report['gates']['encoder_drop_blocks_commands'] is False
    assert report['gates']['dispatch_failure_does_not_move'] is True
    commands.clear()
    report = module.score_faults(summary, frames, commands)
    assert report['gates']['feedback_drop_stops_and_is_unknown'] is True
    assert report['gates']['encoder_drop_blocks_commands'] is True


def test_pixel_loop_is_not_labeled_tensorrt_and_tensorrt_stays_unknown_without_an_engine():
    summary = {'scenarios': [
        {'id': 'pixel-on', 'result': 'FAILED', 'image_detector': 'pixel', 'assist': assist(True)},
        {'id': 'pixel-off', 'result': 'FAILED', 'image_detector': 'pixel', 'assist': assist(False)},
    ]}
    frames = [frame('pixel-on', 0, 1.5, assist_dx=2), frame('pixel-off', 0, 20, assist_dx=0)]
    image = module.score_image(summary, frames)
    assert image['gates']['pixel_on_closer'] is True
    assert image['scope'] == 'cpu_pixel_closed_loop_not_yolo'
    plan = module.tensorrt_plan(None, None)
    assert plan['status'] == 'not_run' and plan['result'] == 'UNKNOWN'
    report = module.build_report(
        {'gates': {'ok': True}, 'scenes': []},
        {'gates': {'ok': True}, 'scenes': []},
        {'gates': {'ok': True}, 'scenes': []},
        image,
        plan,
    )
    assert report['status'] == 'passed'
    assert report['deferred'] == ['reid', 'person_recall_95', 'remote_ci']
    assert math.isfinite(frames[0]['error_px'])
