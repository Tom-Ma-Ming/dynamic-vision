import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('stability', Path(__file__).parents[2]/'tools/audit_sim_stability.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def evidence():
    frames = [dict(scene='s', frame=i, control_valid=i!=3, control_gate='ready' if i!=3 else 'disconnected', error_norm=0.) for i in range(5)]
    metrics = dict(end_stable_frames=1, end_stable=False, post_success_frames=3,
                   post_success_violations=1, first_post_success_violation=3, success_maintained=False)
    scene = dict(id='s', planned_frames=5, success_after_frame=0, settle_frames=2,
                 epsilon_enter=.02, epsilon_exit=.03, terminal_frame=1, result='SUCCESS', metrics=metrics)
    return dict(schema_version=3, scenarios=[scene], aggregate=dict(end_stable=0, success_maintained=0)), frames


def test_latched_success_with_later_disconnect_is_not_maintained():
    summary, frames = evidence()
    assert module.audit(summary, frames)['aggregate']['success_maintained'] == 0


@pytest.mark.parametrize('kind', ['drop_frame', 'false_success', 'invalid_gate', 'old_schema'])
def test_corrupt_stability_evidence_rejected(kind):
    summary, frames = evidence()
    if kind == 'drop_frame': frames.pop(3)
    if kind == 'false_success': summary['scenarios'][0]['metrics']['success_maintained'] = True
    if kind == 'invalid_gate': frames[3]['control_valid'] = True
    if kind == 'old_schema': summary['schema_version'] = 2
    with pytest.raises(ValueError): module.audit(summary, frames)
