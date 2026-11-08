import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('audit_commands', Path(__file__).parents[2] / 'tools/audit_sim_commands.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def event(kind, timestamp=0, **extra):
    return dict(scene='test', id=1, frame_id=0, track_id=1, generated=0,
                arrival=10, expires=20, dx=0, dy=0, kind=kind,
                timestamp=timestamp, **extra)


def test_valid_expiry_and_aggregated_movement():
    rows = [event('generated'), event('accepted', 10), event('applied', 20), event('expired', 20)]
    assert module.audit(rows)['commands'] == 1


@pytest.mark.parametrize('rows', [
    [], [event('accepted', 10)], [event('generated')],
    [event('generated'), event('generated')],
    [event('generated'), event('applied', 10), event('window_end_pending', 20)],
    [event('generated'), event('dispatch_failed'), event('accepted', 10)],
    [event('generated'), event('accepted', 9), event('expired', 20)],
    [event('generated'), event('accepted', 10), event('window_end_pending', 20)],
    [event('generated'), event('dispatch_failed'), event('dispatch_failed')],
])
def test_corrupt_evidence_rejected(rows):
    with pytest.raises(ValueError):
        module.audit(rows)


def test_scene_local_ids_are_independent():
    rows = [event('generated'), event('dispatch_failed')]
    assert module.audit(rows + [dict(row, scene='other') for row in rows])['commands'] == 2
