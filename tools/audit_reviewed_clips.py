"""Audit reviewed clip labels without changing source images or annotations."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path

import cv2


def audit(folder):
    manifest = json.loads((folder/'manifest.json').read_text(encoding='utf-8-sig'))
    issues, files = [], []
    counts, identities = Counter(), set()
    empty = 0
    for entry in manifest['mapping']:
        image = folder/entry['image']
        annotation = image.with_suffix('.json')
        def issue(kind, detail):
            issues.append(dict(file=annotation.name,kind=kind,detail=detail))
        if not image.is_file() or not annotation.is_file():
            issue('missing_pair','Image or JSON missing')
            continue
        data = json.loads(annotation.read_text(encoding='utf-8-sig'))
        pixels = cv2.imread(str(image))
        if pixels is None:
            issue('image_decode','Image cannot be decoded')
            continue
        h,w = pixels.shape[:2]
        if (data.get('imagePath'),data.get('imageWidth'),data.get('imageHeight')) != (image.name,w,h):
            issue('metadata','Image path/size does not match')
        shapes = data.get('shapes',[])
        empty += not shapes
        ids = Counter()
        for index,shape in enumerate(shapes,1):
            label,group = shape.get('label'),shape.get('group_id')
            counts[label] += 1
            if label not in ('head','person'):
                issue('class',f'shape {index}: {label}')
            if type(group) is not int or group < 0:
                issue('identity',f'shape {index}: missing/invalid group_id {group}')
            else:
                identities.add(group)
                ids[(label,group)] += 1
            points = shape.get('points',[])
            if shape.get('shape_type')!='rectangle' or len(points) not in (2,4):
                issue('shape',f'shape {index}: expected rectangle')
                continue
            if any(len(p)!=2 or not all(isinstance(v,(int,float)) and math.isfinite(v) for v in p) for p in points):
                issue('coordinates',f'shape {index}: invalid coordinates')
                continue
            xs,ys = zip(*points)
            if min(xs)>=max(xs) or min(ys)>=max(ys):
                issue('area',f'shape {index}: nonpositive rectangle')
            if min(xs)<0 or min(ys)<0 or max(xs)>w or max(ys)>h:
                issue('bounds',f'shape {index}: rectangle outside image')
        for (label,group),count in ids.items():
            if count>1:
                issue('duplicate_identity',f'{label} ID {group}: {count} shapes')
        files.append(dict(image=image.name,source_frame=entry['source_frame'],
                          image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                          annotation_sha256=hashlib.sha256(annotation.read_bytes()).hexdigest()))
    return dict(clip=folder.name,source=str(folder.resolve()),planned_frames=len(manifest['mapping']),
                paired_frames=len(files),classes=dict(counts),empty_frames=empty,identities=sorted(identities),
                structural_errors=len(issues),issues=issues,files=files,
                note='Structural checks do not establish visible-body coverage, identity continuity or ground-truth approval.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('output',type=Path)
    args = p.parse_args()
    folders = sorted(p for p in args.root.glob('clip*') if (p/'manifest.json').is_file())
    if not folders:
        raise ValueError('No clip manifests')
    report = dict(clips=[audit(folder) for folder in folders])
    report['structural_errors'] = sum(c['structural_errors'] for c in report['clips'])
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(report,stream,indent=2,ensure_ascii=False)
    for clip in report['clips']:
        print({k:clip[k] for k in ('clip','paired_frames','classes','empty_frames','identities','structural_errors')})
    print('Structural errors:',report['structural_errors'])
    return 1 if report['structural_errors'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
