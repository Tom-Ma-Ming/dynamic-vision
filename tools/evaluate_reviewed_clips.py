"""Snapshot reviewed clips and compute provisional detection and MOT diagnostics."""
import argparse
import hashlib
from importlib.metadata import version
import json
import re
from pathlib import Path
import shutil

import cv2
import numpy as np
from scipy.optimize import linear_sum_assignment

from audit_reviewed_clips import audit
from score_mot import score


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def rectangle(shape):
    points=np.asarray(shape['points'],dtype=float)
    return [*points.min(axis=0),*points.max(axis=0)]


def detection_counts(gt,pred,threshold=.5):
    if not gt or not pred:
        return dict(tp=0,fp=len(pred),fn=len(gt))
    a=np.asarray(gt)[:,None,:];b=np.asarray(pred)[None,:,:]
    intersection=np.maximum(0,np.minimum(a[...,2:],b[...,2:])-np.maximum(a[...,:2],b[...,:2])).prod(axis=2)
    union=(a[...,2:]-a[...,:2]).prod(axis=2)+(b[...,2:]-b[...,:2]).prod(axis=2)-intersection
    iou=intersection/np.maximum(union,1e-12)
    valid=iou>=threshold
    # Maximize number of valid pairs first, then their total IoU.
    weights=np.where(valid,min(len(gt),len(pred))+1+iou,0)
    rows,columns=linear_sum_assignment(-weights)
    tp=int(valid[rows,columns].sum())
    return dict(tp=tp,fp=len(pred)-tp,fn=len(gt)-tp)


def rates(counts):
    return {**counts,'precision':counts['tp']/(counts['tp']+counts['fp']) if counts['tp']+counts['fp'] else None,
            'recall':counts['tp']/(counts['tp']+counts['fn']) if counts['tp']+counts['fn'] else None}


def aligned_rows(entries,rows):
    for entry in entries:
        frame=entry['source_frame']
        if not (0<=frame<len(rows)) or rows[frame]['frame_id']!=frame:
            raise ValueError('Prediction frame mapping mismatch')
        row=rows[frame]
        if row['source_ts_ns']!=entry['source_ts_ns']:
            raise ValueError('Prediction timestamp mismatch')
        yield row


def evaluate_clip(folder,replay,output,method_names=('cv','imm')):
    check=audit(folder)
    if check['structural_errors']:
        raise ValueError(f'{folder}: structural label errors')
    target=output/'labels'/folder.name
    target.mkdir(parents=True)
    manifest=json.loads((folder/'manifest.json').read_text())
    entries=manifest['mapping']
    frames=[e['source_frame'] for e in entries]
    if not frames or frames!=list(range(frames[0],frames[0]+len(frames))):
        raise ValueError('Labels must cover a consecutive source window')
    expected={f['image']:f for f in check['files']}
    for entry in entries:
        for name in (entry['image'],str(Path(entry['image']).with_suffix('.json'))):
            shutil.copyfile(folder/name,target/name)
        record=expected[entry['image']]
        if digest(target/entry['image'])!=record['image_sha256'] or digest((target/entry['image']).with_suffix('.json'))!=record['annotation_sha256']:
            raise ValueError('Labels changed during snapshot')
    shutil.copyfile(folder/'manifest.json',target/'manifest.json')
    # Verify images really belong to the claimed source frames.
    video=Path(manifest['source_video'])
    capture=cv2.VideoCapture(str(video))
    try:
        if not capture.isOpened() or not capture.set(cv2.CAP_PROP_POS_FRAMES,frames[0]):
            raise ValueError('Cannot open source video at the requested frame')
        for entry in entries:
            ok,image=capture.read()
            if not ok or not np.array_equal(image,cv2.imread(str(target/entry['image']))):
                raise ValueError('Source pixels differ from annotated PNG')
    finally:
        capture.release()
    annotations=[json.loads((target/entry['image']).with_suffix('.json').read_text(encoding='utf-8-sig')) for entry in entries]
    detection={name:dict(tp=0,fp=0,fn=0) for name in ('head','person')}
    methods={};reference=None;inputs={}
    for method in method_names:
        path=replay/f'{video.stem}-{method}'/'frames.jsonl'
        all_rows=[json.loads(line) for line in path.read_text().splitlines()]
        rows=list(aligned_rows(entries,all_rows))
        stable=[(r['frame_id'],r['source_ts_ns'],r['detections']) for r in rows]
        if reference is not None and reference!=stable:
            raise ValueError('Compared methods have different detections')
        reference=stable
        mot_rows=[]
        for entry,annotation,row in zip(entries,annotations,rows):
            if method==method_names[0]:
                for cls,name in enumerate(('head','person')):
                    gt=[rectangle(s) for s in annotation['shapes'] if s['label']==name]
                    pred=[d['xyxy'] for d in row['detections'] if d['class_id']==cls and d['score']>=.25]
                    counts=detection_counts(gt,pred)
                    for key,value in counts.items():detection[name][key]+=value
            mot_rows.append(dict(source_frame=entry['source_frame'],source_ts_ns=row['source_ts_ns'],
                                 gt=[[s['group_id'],*rectangle(s)] for s in annotation['shapes'] if s['label']=='person'],
                                 tracks=[[t['id'],*t['xyxy']] for t in row['tracks'] if t['state']=='Confirmed']))
        (output/f'{folder.name}-{method}-aligned.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in mot_rows),encoding='utf-8')
        methods[method]=score(mot_rows)
        inputs[method]=dict(path=str(path.resolve()),sha256=digest(path))
    return dict(clip=folder.name,frames=len(entries),source_video=str(video),source_sha256=digest(video),
                source_frames=[frames[0],frames[-1]+1],annotation_audit=check,prediction_inputs=inputs,
                detection={k:rates(v) for k,v in detection.items()},tracking=methods)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('labels',type=Path)
    parser.add_argument('replay',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--quality-notes',type=Path,required=True,help='Explicit visual acceptance status and unresolved issues')
    parser.add_argument('--methods',nargs='+',default=['cv','imm'],help='Replay directory suffixes to compare')
    args=parser.parse_args()
    if len(args.methods)<2 or len(set(args.methods))!=len(args.methods) or any(not re.fullmatch('[a-z0-9_-]+',name) for name in args.methods):
        raise ValueError('Provide at least two unique simple method names')
    folders=sorted(p for p in args.labels.glob('clip*') if (p/'manifest.json').is_file())
    if not folders:raise ValueError('No reviewed clips')
    quality=json.loads(args.quality_notes.read_text(encoding='utf-8-sig'))
    args.output.mkdir(parents=True,exist_ok=False)
    report=dict(status='provisional_diagnostic_not_acceptance',quality=quality,
                environment={name:version(name) for name in ('numpy','scipy','opencv-python','trackeval')},
                evaluator_sha256=digest(Path(__file__)),scorer_sha256=digest(Path(__file__).with_name('score_mot.py')),
                protocol=dict(detection_confidence=.25,detection_iou=.5,detection_matching='maximum-cardinality then maximum IoU',
                              tracker_states=['Confirmed'],ambiguous_tracks_included=True,
                              tracker_box_semantics='All unified tracker boxes, including any head-origin tracks; no class filtering available in replay JSON.',
                              identity_scope='per clip',tracker_warm_start='state from full source video; score only annotated window',
                              HOTA='TrackEval default IoU thresholds 0.05..0.95',IDF1_IDSW_iou=.5),clips=[])
    for folder in folders:
        clip=evaluate_clip(folder,args.replay,args.output,args.methods);report['clips'].append(clip)
        print(json.dumps({k:clip[k] for k in ('clip','frames','detection','tracking')}),flush=True)
    totals={name:rates({key:sum(c['detection'][name][key] for c in report['clips']) for key in ('tp','fp','fn')}) for name in ('head','person')}
    report['detection_micro_totals']=totals
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Detection totals:',json.dumps(totals))


if __name__=='__main__':
    main()
