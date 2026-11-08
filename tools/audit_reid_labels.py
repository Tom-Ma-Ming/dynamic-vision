"""Inventory local identity annotations without inferring cross-clip identities."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics


def inventory(folder, gap_seconds=.5):
    manifest_path=folder/'manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    fps=manifest['fps']
    if not math.isfinite(fps) or fps<=0 or not math.isfinite(gap_seconds) or gap_seconds<=0:
        raise ValueError('Invalid time scale')
    mapping=manifest['mapping']
    frames=[e['source_frame'] for e in mapping]
    if not frames or frames!=list(range(frames[0],frames[0]+len(frames))):
        raise ValueError('Expected a consecutive annotated source window')
    tracks=defaultdict(list);hashes=[];co_visible=0
    for entry in mapping:
        relative=Path(entry['image']).with_suffix('.json')
        if relative.name!=str(relative):raise ValueError('Annotation must be directly inside clip directory')
        path=folder/relative
        raw=path.read_bytes();annotation=json.loads(raw.decode('utf-8-sig'))
        hashes.append(dict(file=relative.name,sha256=hashlib.sha256(raw).hexdigest()))
        seen=set()
        for shape in annotation['shapes']:
            if shape['label']!='person':continue
            identity=shape.get('group_id')
            if type(identity) is not int or identity in seen:
                raise ValueError('Missing/noninteger/duplicate person identity within frame')
            seen.add(identity)
            points=shape['points']
            if shape['shape_type']!='rectangle' or len(points) not in (2,4) or any(len(p)!=2 for p in points):
                raise ValueError('Expected rectangle person annotation')
            xs,ys=zip(*points)
            if not all(math.isfinite(v) for v in (*xs,*ys)):raise ValueError('Nonfinite box')
            width,height=max(xs)-min(xs),max(ys)-min(ys)
            if min(width,height)<=0:raise ValueError('Degenerate box')
            tracks[identity].append((entry['source_frame'],width,height))
        co_visible+=len(seen)*(len(seen)-1)//2
    identities=[]
    for identity,rows in sorted(tracks.items()):
        gaps=[dict(before=a[0],after=b[0],missing_frames=b[0]-a[0]-1,
                   missing_seconds=(b[0]-a[0]-1)/fps)
              for a,b in zip(rows,rows[1:]) if b[0]>a[0]+1]
        identities.append(dict(identity=f'{folder.name}:{identity}',annotations=len(rows),
                               first_frame=rows[0][0],last_frame=rows[-1][0],
                               median_width=statistics.median(r[1] for r in rows),
                               median_height=statistics.median(r[2] for r in rows),
                               height_below_32=sum(r[2]<32 for r in rows),
                               annotation_gaps=gaps,
                               long_gap_candidates=sum(g['missing_seconds']>=gap_seconds for g in gaps)))
    return dict(clip=folder.name,source_video=manifest['source_video'],fps=fps,
                frames=len(mapping),identities=identities,
                co_visible_identity_pairs=co_visible,
                manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),annotations=hashes)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('labels',type=Path)
    parser.add_argument('output',type=Path,help='New report file')
    parser.add_argument('--gap-seconds',type=float,default=.5)
    args=parser.parse_args()
    clips=[inventory(p,args.gap_seconds) for p in sorted(args.labels.glob('clip*')) if (p/'manifest.json').is_file()]
    if not clips:raise ValueError('No identity clips')
    ids=[identity for clip in clips for identity in clip['identities']]
    report=dict(status='diagnostic_inventory_not_reid_acceptance',identity_scope='clip_local_only',
                cross_clip_identity_links='unconfirmed',gap_seconds=args.gap_seconds,
                caveat='Annotation gaps are not verified occlusions; co-visible pairs require identity review.',
                clips=clips,totals=dict(clips=len(clips),identities=len(ids),
                    person_annotations=sum(i['annotations'] for i in ids),
                    long_gap_candidates=sum(i['long_gap_candidates'] for i in ids),
                    height_below_32=sum(i['height_below_32'] for i in ids)))
    with args.output.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2,ensure_ascii=False)
    print(json.dumps(report['totals']))


if __name__=='__main__':main()
