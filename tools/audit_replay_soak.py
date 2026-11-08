"""Independently check the one-hour local replay gates fixed in replay-soak spec."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import cv2


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        raise ValueError('Missing samples')
    return ordered[math.ceil(len(ordered)*fraction)-1]


def verify_inputs(root, manifest):
    args=manifest['arguments']
    expected=[manifest['executable'],args[0],args[1],args[3],args[5]]
    entries=manifest['inputs']
    if [str(Path(e['path']).resolve()) for e in entries]!=[str(Path(p).resolve()) for p in expected]:return False
    copies=None
    archive=root/'input-archive.json'
    if archive.exists():
        saved=json.loads(archive.read_text(encoding='utf-8'))
        if saved['original_manifest_sha256']!=hashlib.sha256((root/'manifest.json').read_bytes()).hexdigest():return False
        copies=saved['inputs']
        if len(copies)!=len(entries):return False
    for i,entry in enumerate(entries):
        path=Path(entry['path'])
        if copies is not None:
            copy=copies[i]
            if copy['source_path']!=entry['path'] or copy['sha256'].lower()!=entry['sha256'].lower():return False
            path=(root/copy['file']).resolve()
            try:path.relative_to((root/'input-archive').resolve())
            except ValueError:return False
        with path.open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest().lower()!=entry['sha256'].lower():return False
    return True


def summary_gates(summary, completion):
    names = ('captured','processed','replaced','rejected','pending','stale','failed','peak','capacity')
    counters_valid = all(type(summary[k]) is int and summary[k]>=0 for k in names)
    elapsed = completion['elapsed_s']
    return dict(completion=completion['exit_code']==0 and math.isfinite(elapsed) and elapsed>=3590,
                configuration=summary['status']=='ok' and summary['mode']=='latest' and
                              summary['paced'] is True and summary['capacity']==1,
                counters=counters_valid,
                captured=summary['captured']==108000,
                accounting=summary['captured']==sum(summary[k] for k in ('processed','replaced','rejected','pending','stale','failed')),
                clean_shutdown=all(summary[k]==0 for k in ('pending','failed','rejected','stale')),
                queue=counters_valid and summary['peak']<=summary['capacity'] and
                      summary['replaced']/max(1,summary['captured'])<=.01)


def resource_gates(resources, elapsed):
    valid = bool(resources) and math.isfinite(elapsed)
    previous_time, previous_cpu, pid = -1, -1, None
    for row in resources:
        values = [row[k] for k in ('elapsed_s','cpu_seconds','private_bytes','working_set_bytes','handles')]
        valid &= all(math.isfinite(v) and v>=0 for v in values)
        valid &= previous_time<row['elapsed_s']<=elapsed and row['cpu_seconds']>=previous_cpu
        valid &= type(row['pid']) is int and row['pid']>0 and (pid is None or row['pid']==pid)
        previous_time,previous_cpu,pid = row['elapsed_s'],row['cpu_seconds'],row['pid']
    early = [r['private_bytes'] for r in resources if 300<=r['elapsed_s']<600]
    late = [r['private_bytes'] for r in resources if elapsed-300<=r['elapsed_s']<=elapsed]
    delta = (statistics.median(late)-statistics.median(early))/2**20 if early and late else None
    return dict(resources_valid=valid,
                memory=valid and len(early)>=240 and len(late)>=240 and delta is not None and delta<=32), delta, [len(early),len(late)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    root = args.run
    read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
    summary = read(root/'replay/summary.json')
    manifest = read(root/'manifest.json')
    completion = read(root/'completion.json')
    resources = [json.loads(s) for s in (root/'resources.jsonl').read_text().splitlines()]
    elapsed = completion['elapsed_s']
    gates = summary_gates(summary, completion)
    gates['input_hashes'] = verify_inputs(root,manifest)
    keys = ('queue_wait_ms','infer_ms','mot_ms','result_age_ms')
    samples = {k:[] for k in keys}
    count = 0
    previous = (-1,-1)
    monotonic = True
    with (root/'replay/frames.jsonl').open() as stream:
        for line in stream:
            row = json.loads(line)
            current = row['frame_id'],row['source_ts_ns']
            monotonic &= all(a<b for a,b in zip(previous,current))
            previous = current
            for key in keys:
                samples[key].append(row[key])
            count += 1
    gates['frames'] = monotonic and count==summary['processed'] and count>0
    exact = {}
    statistics_ok = summary.get('latency_quantiles')=='log_histogram_upper_bound_1pct_plus_2ns'
    for key,values in samples.items():
        exact[key] = {name:percentile(values,q) for name,q in [('p50',.5),('p95',.95),('p99',.99)]}
        statistics_ok &= summary[key]['n']==count and math.isclose(summary[key]['max'],max(values),rel_tol=1e-5,abs_tol=1e-5)
        for name,value in exact[key].items():
            tolerance = max(1e-5,value*1e-5)
            statistics_ok &= value-tolerance<=summary[key][name]<=value*1.01+.000002+tolerance
    gates['statistics'] = statistics_ok
    gates['latency'] = exact['result_age_ms']['p99']<=33.334
    resource_checks,delta,window_samples = resource_gates(resources, elapsed)
    gates.update(resource_checks)
    cap = cv2.VideoCapture(str(root/'replay/overlay.mp4'))
    decoded = 0
    try:
        while cap.read()[0]:
            decoded += 1
    finally:
        cap.release()
    gates['video'] = decoded==count
    report = dict(status='passed' if all(gates.values()) else 'failed',gates=gates,
                  exact_latency_ms=exact,private_growth_mib=delta,
                  memory_window_samples=window_samples,decoded=decoded,
                  scope='local_one_hour_resource_test_not_overall_acceptance')
    (root/'audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))
    return 0 if all(gates.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
