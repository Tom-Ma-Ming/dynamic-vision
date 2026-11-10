"""YOLO ONNX closed loop: the same human-led trim, driven by a real YOLO graph.

The model must be an ONNX YOLO file. A 6-channel graph is the project
head/person contract. A wider graph is treated as COCO and only its person
channel is used. This does not run TensorRT. Region, dead zone, step and
resume wait stay the experimental values in the scenario file.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def digest(path):
    hasher = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def load(folder):
    summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8'))
    frames = [json.loads(line) for line in (folder / 'runs.jsonl').read_text(encoding='utf-8').splitlines() if line]
    grouped = {}
    for row in frames:
        grouped.setdefault(row['scene'], []).append(row)
    return summary, grouped


def final_error(rows):
    return rows[-1]['error_px']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runner', type=Path)
    parser.add_argument('tracker', type=Path)
    parser.add_argument('model', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scenarios', type=Path, default=Path('configs/sim-yolo-center.yml'))
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('output directory must be new')
    args.output.mkdir(parents=True)
    completed = subprocess.run(
        [str(args.runner), str(args.scenarios), str(args.output / 'run'), str(args.tracker), str(args.model)],
        capture_output=True, text=True)
    (args.output / 'process.log').write_text(completed.stdout + completed.stderr, encoding='utf-8')
    if completed.returncode != 0:
        raise SystemExit(f'sim_runner exit {completed.returncode}: {completed.stderr}')
    summary, frames = load(args.output / 'run')
    on, off = frames['yolo-on'], frames['yolo-off']
    meta = {scene['id']: scene for scene in summary['scenarios']}
    gates = {
        'backend_is_yolo_onnx': summary.get('detector_backend') == 'yolo-onnx',
        'graph_ran': summary.get('yolo_channels', 0) >= 6 and summary.get('yolo_person_frames', 0) > 0,
        'scenes_request_yolo': meta['yolo-on'].get('image_detector') == 'yolo' and meta['yolo-off'].get('image_detector') == 'yolo',
        'off_has_no_trim': sum((row['assist_dx_px'] ** 2 + row['assist_dy_px'] ** 2) ** 0.5 for row in off) < 1e-6,
        'on_closer_than_off': final_error(on) + 5 < final_error(off),
        'on_inside_experimental_region': final_error(on) < meta['yolo-on']['assist']['enter_px'],
        'parameters_experimental': meta['yolo-on']['assist'].get('parameters_experimental') is True,
        'not_tensorrt': True,
    }
    report = {
        'protocol': 'yolo-onnx-closed-loop-v1',
        'status': 'passed' if all(gates.values()) else 'failed',
        'gates': gates,
        'model_sha256': digest(args.model),
        'detector_backend': summary.get('detector_backend'),
        'yolo_channels': summary.get('yolo_channels'),
        'yolo_anchors': summary.get('yolo_anchors'),
        'yolo_input_px': summary.get('yolo_input_px'),
        'yolo_project_contract': summary.get('yolo_project_contract'),
        'yolo_person_frames': summary.get('yolo_person_frames'),
        'on_final_error_px': final_error(on),
        'off_final_error_px': final_error(off),
        'on_legacy_result': meta['yolo-on']['result'],
        'off_legacy_result': meta['yolo-off']['result'],
        'experimental_note': 'enter/exit, deadzone, step and resume wait are experimental tunables.',
        'tensorrt': {
            'status': 'blocked',
            'result': 'UNKNOWN',
            'reason': 'This machine has no NVIDIA GPU (no /dev/nvidia* and no nvidia-smi). TensorRT cannot execute a model here. The Windows recipe remains vision_sim_image <engine> ... human_led.',
        },
    }
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'status': report['status'], 'on': report['on_final_error_px'], 'off': report['off_final_error_px'],
                      'person_frames': report['yolo_person_frames'], 'channels': report['yolo_channels']}))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
