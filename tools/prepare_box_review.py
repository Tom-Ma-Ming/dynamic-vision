"""Generate review-only head/person annotations from the two known source schemas."""
import argparse
import csv
import html
import itertools
import shutil
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

from import_screenshots import parse_label


def corners(row):
    _, x, y, w, h = parse_label(row)
    return (x-w/2, y-h/2, x+w/2, y+h/2)


def pair_cost(head, body, legacy):
    l, t, r, b = body
    hx = (head[0]+head[2])/2
    dx = abs(hx-(l+r)/2)/(r-l)
    dy = abs((head[3] if legacy else head[1])-t)/(b-t)
    if dx > .85 or dy > .65 or (head[1]+head[3])/2 > (t+b)/2:
        return None
    return dx+dy


def assign(heads, bodies, legacy):
    # These small reviewed batches have at most a handful of subjects per frame.
    choices = []
    for head in heads:
        candidates = [(i, pair_cost(head, body, legacy)) for i, body in enumerate(bodies)]
        choices.append([(None, 0)] + [(i, cost) for i, cost in candidates if cost is not None])
    best = None
    for assignment in itertools.product(*choices):
        ids = [i for i, _ in assignment if i is not None]
        if len(ids) != len(set(ids)):
            continue
        rank = (-len(ids), sum(cost for _, cost in assignment))
        if best is None or rank < best[0]:
            best = rank, assignment
    return [i for i, _ in best[1]]


def overlay(im, boxes):
    preview = im.copy()
    draw = ImageDraw.Draw(preview)
    for cls, (l,t,r,b) in sorted(boxes, reverse=True):
        draw.rectangle((l*im.width,t*im.height,r*im.width,b*im.height),
                       outline=("yellow","cyan")[cls], width=1)
    return preview


def generate(old, new, out):
    out.mkdir(parents=True, exist_ok=False)
    records = []
    for split, source in [('train',old), ('val',new)]:
        folder = out / split
        folder.mkdir()
        (folder/'classes.txt').write_text('head\nperson\n')
        previews = out/'preview'/split
        previews.mkdir(parents=True)
        for img in sorted(source.glob('*.jpg')):
            rows = img.with_suffix('.txt').read_text(encoding='utf-8-sig').splitlines()
            ids = [int(s.split()[0]) ^ int(split=='train') for s in rows]
            boxes = [corners(s) for s in rows]
            heads = [box for box,c in zip(boxes,ids) if c==0]
            body_ids = [j for j,c in enumerate(ids) if c==1]
            matches = assign(heads,[boxes[i] for i in body_ids],split=='train')
            updated = boxes.copy()
            notes = []
            if len(heads)>1 or len(body_ids)>1:
                notes.append('多人配对')
            if None in matches or len(set(matches)-{None})<len(body_ids):
                notes.append('头身未配齐')
            with Image.open(img) as raw:
                im = raw.convert('RGB')
            max_change = 0
            for h,m in zip(heads,matches):
                if m is None:
                    continue
                j = body_ids[m]
                l,t,r,b = boxes[j]
                u = (min(l,h[0]),min(t,h[1]),max(r,h[2]),max(b,h[3]))
                updated[j] = u
                max_change=max(max_change,*(abs(i-k)*s for i,k,s in zip(u,boxes[j],im.size*2)))
            if split=='val' and max_change>1.001:
                notes.append('边界超过1像素')
            lines=[]
            for row,c,orig,box in zip(rows,ids,boxes,updated):
                l,t,r,b=box
                coords=(row.split(maxsplit=1)[1] if box==orig else
                        f'{(l+r)/2:.9f} {(t+b)/2:.9f} {r-l:.9f} {b-t:.9f}')
                line=f'{c} {coords}'
                parse_label(line)
                lines.append(line)
            (folder/img.with_suffix('.txt').name).write_text(''.join(s+'\n' for s in lines))
            shutil.copyfile(img,folder/img.name)
            pair=Image.new('RGB',(2*im.width,im.height))
            for col,rects in enumerate([boxes,updated]):
                pair.paste(overlay(im,zip(ids,rects)),(col*im.width,0))
            preview=previews/(img.stem+'.png')
            pair.save(preview)
            records.append((split,img.name,'、'.join(notes) or '常规抽查',
                            round(max_change,3),preview.relative_to(out).as_posix()))
    records.sort(key=lambda r:(r[2]=='常规抽查',r[0],r[1]))
    with (out/'review.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.writer(f)
        writer.writerow(['数据集','图片','复核原因','人物框最大扩张像素','预览'])
        writer.writerows(records)
    page=['<!doctype html><meta charset=utf-8><title>人物框复核</title>',
          '<style>body{font:16px system-ui;max-width:780px;margin:auto}img{max-width:100%}article{border-top:1px solid #aaa;padding:8px}</style>',
          '<h1>人物框复核</h1><p>左侧原框，右侧候选框。黄色为头，青色为人物。优先检查同一个人的头身是否配对，再看大框是否覆盖全部可见人体。未配齐的框保持原位置，请对照原图判断。候选标签可按 YOLO HBB 导入修改。</p>']
    for split,name,note,gap,preview in records:
        page.append(f'<article>{split} / {html.escape(name)}　{note}　扩张 {gap:g} px<br><img loading="lazy" src="{html.escape(preview)}"></article>')
    (out/'index.html').write_text('\n'.join(page),encoding='utf-8')
    print(Counter((r[0],r[2]) for r in records))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ('old','new','out'):
        parser.add_argument(arg,type=Path)
    args=parser.parse_args()
    generate(args.old,args.new,args.out)
