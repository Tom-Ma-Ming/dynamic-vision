"""Check camera-compensated replay integrity and summarize estimation coverage."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path

from verify_recorded_replays import verify_run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('experiment',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    results=[]
    folders=sorted(args.experiment.glob('*-camera'))
    if not folders:raise ValueError('No camera replay runs')
    for folder in folders:
        stem=folder.name.removesuffix('-camera')
        baseline=[json.loads(line) for line in (args.baseline/f'{stem}-cv'/'frames.jsonl').read_text().splitlines()]
        rows,stats=verify_run(folder,len(baseline),False)
        reasons=Counter()
        for index,(a,b) in enumerate(zip(baseline,rows)):
            for field in ('frame_id','source_ts_ns','timestamp_source','detections'):
                assert a[field]==b[field],(stem,index,field)
            c=b['camera_motion'];reasons[c['reason']]+=1
            assert c['to_ts_ns']==b['source_ts_ns']
            assert c['from_ts_ns']==(rows[index-1]['source_ts_ns'] if index else -1)
            assert all(math.isfinite(c[k]) for k in ('dx_px','dy_px','position_variance_px2','inlier_ratio','coverage'))
            assert 0<=c['inliers']<=c['features'] and 0<=c['inlier_ratio']<=1 and 0<=c['coverage']<=1
            if c['features']:
                assert abs(c['inlier_ratio']-c['inliers']/c['features'])<1e-5
            assert c['accepted']==(c['reason']=='accepted')
            if not c['accepted']:
                assert c['dx_px']==0 and c['dy_px']==0 and c['position_variance_px2']==0
            else:
                assert c['position_variance_px2']>=.25
            assert math.isfinite(b['camera_ms']) and b['camera_ms']>=0
            assert b['result_age_ms']+.02>=b['infer_ms']+b['mot_ms']+b['camera_ms']
        summary=json.loads((folder/'summary.json').read_text())
        assert summary['camera_ms']['n']==len(rows)
        stats.update(video=stem,camera_accepted_frames=reasons['accepted'],
                     camera_accepted_fraction=reasons['accepted']/len(rows),
                     camera_reasons=dict(reasons),camera_ms=summary['camera_ms'],
                     note='Acceptance is an internal quality gate, not measured camera-motion accuracy.')
        results.append(stats);print(json.dumps(stats),flush=True)
    with args.output.open('x',encoding='utf-8') as stream:json.dump(results,stream,indent=2)


if __name__=='__main__':main()
