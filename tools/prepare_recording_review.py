"""Export a consecutive review clip with raw PNGs and unreviewed X-AnyLabeling boxes."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('video', type=Path)
    parser.add_argument('cv_run', type=Path)
    parser.add_argument('imm_run', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--frames', type=int, default=90)
    args = parser.parse_args()
    if args.start < 0 or args.frames < 1:
        raise ValueError('Invalid frame window')
    rows = [json.loads(line) for line in (args.cv_run/'frames.jsonl').read_text().splitlines()]
    if args.start+args.frames > len(rows):
        raise ValueError('Window exceeds source')
    args.output.mkdir(parents=True, exist_ok=False)
    captures = [cv2.VideoCapture(str(path)) for path in
                (args.video,args.cv_run/'overlay.mp4',args.imm_run/'overlay.mp4')]
    writer = None
    mapping = []
    try:
        assert all(c.isOpened() for c in captures)
        fps = captures[0].get(cv2.CAP_PROP_FPS)
        width, height = int(captures[0].get(3)), int(captures[0].get(4))
        writer = cv2.VideoWriter(str(args.output/'comparison.mp4'),cv2.VideoWriter_fourcc(*'mp4v'),
                                fps,(width*3,height+28))
        assert writer.isOpened()
        for cap in captures:
            assert cap.set(cv2.CAP_PROP_POS_FRAMES,args.start)
        for index in range(args.start,args.start+args.frames):
            images = []
            for cap in captures:
                ok, frame = cap.read()
                assert ok
                images.append(frame)
            name = f'{args.video.stem}_f{index:06d}.png'
            assert cv2.imwrite(str(args.output/name),images[0])
            shapes = []
            for det in rows[index]['detections']:
                left,top,right,bottom = det['xyxy']
                left,right = max(0.,left),min(float(width),right)
                top,bottom = max(0.,top),min(float(height),bottom)
                if right <= left or bottom <= top:
                    continue
                shapes.append(dict(label=('head','person')[det['class_id']],
                                   points=[[left,top],[right,top],[right,bottom],[left,bottom]],
                                   group_id=None, description=f"UNREVIEWED detector score={det['score']:.6f}",
                                   difficult=False,shape_type='rectangle',flags={},attributes={}))
            annotation = dict(version='2.5.0',flags={},shapes=shapes,imagePath=name,
                              imageData=None,imageHeight=height,imageWidth=width)
            (args.output/Path(name).with_suffix('.json')).write_text(json.dumps(annotation,indent=2),encoding='utf-8')
            panels = []
            for label,im in zip(('RAW','CV','IMM'),images):
                panel = cv2.copyMakeBorder(im,28,0,0,0,cv2.BORDER_CONSTANT)
                cv2.putText(panel,f'{label} | frame {index} | {index/fps:.2f}s',(5,19),0,.45,(255,255,255),1)
                panels.append(panel)
            writer.write(np.hstack(panels))
            mapping.append(dict(image=name,source_frame=index,source_ts_ns=rows[index]['source_ts_ns']))
    finally:
        for cap in captures:
            cap.release()
        if writer is not None:
            writer.release()
    (args.output/'manifest.json').write_text(json.dumps(dict(
        source_video=str(args.video.resolve()),start_frame=args.start,frames=args.frames,
        fps=fps,annotation_status='unreviewed_detection_proposals_no_identity',
        selection='diagnostic_sample_not_random_test_set',mapping=mapping),indent=2),encoding='utf-8')
    (args.output/'classes.txt').write_text('head\nperson\n',encoding='utf-8')
    (args.output/'README.md').write_text(
        '# 连续录像复核包\n\n'
        'comparison.mp4 从左到右为原图、CV、IMM。PNG 是连续原始帧，JSON 是同名 X-AnyLabeling 候选框。\n\n'
        '候选框尚未人工复核，可能漏框或误框；所有 group_id 为空，没有把自动跟踪 ID 当身份真值。\n\n'
        '使用之前能打开的图像目录/图像标记方式加载同名 PNG+JSON，不要把这些 JSON 当 YOLO HBB 文本导入。'
        '类别仍写 head 和 person。先删除误框、补漏框、修改框；人物框包含头部。\n\n'
        '身份复核时，同一人物跨帧使用同一 group_id（组 ID），对应 head/person 使用相同 ID；不同人物不能共用。'
        '短暂遮挡后能确定是原人物才沿用 ID；无法确定时另记待确认，不凭自动 ID 猜测。完全遮挡时不要凭空补框。'
        '本包是困难样例，不能单独用于宣称整体召回或身份准确率。\n\n'
        '修改后保留原文件名及 JSON，另存到 reviewed 子目录；不要仅导出 YOLO HBB，后者不保留身份 ID。\n',
        encoding='utf-8')
    print(f'Prepared {args.frames} consecutive images and JSONs: {args.output}')


if __name__ == '__main__':
    main()
