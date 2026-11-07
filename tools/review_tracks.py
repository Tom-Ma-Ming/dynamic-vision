"""Interactive review: click a confirmed ID, space pauses, arrows step, Esc exits."""
import argparse
import json
from pathlib import Path
import cv2


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("video",type=Path)
    p.add_argument("jsonl",type=Path)
    a=p.parse_args()
    rows=[json.loads(s) for s in a.jsonl.read_text().splitlines()]
    cap=cv2.VideoCapture(str(a.video))
    if not rows or not cap.isOpened():
        raise ValueError("Missing video or tracking frames")
    selected=None
    frame_id=0
    paused=True
    window="Track review"
    scale=2
    def click(event,x,y,flags,param):
        nonlocal selected
        if event==cv2.EVENT_RBUTTONDOWN:
            selected=None
        if event!=cv2.EVENT_LBUTTONDOWN:
            return
        choices=[]
        for t in rows[frame_id]["tracks"]:
            l,top,r,b=t["xyxy"]
            if t["state"]=="Confirmed" and not t["identity_ambiguous"] and l<=x/scale<=r and top<=y/scale<=b:
                choices.append(((r-l)*(b-top),t["id"]))
        if choices:
            selected=min(choices)[1]
    cv2.namedWindow(window,cv2.WINDOW_AUTOSIZE)
    cv2.setMouseCallback(window,click)
    cached=-1
    try:
        while True:
            if cached!=frame_id:
                if frame_id!=cached+1:
                    cap.set(cv2.CAP_PROP_POS_FRAMES,frame_id)
                ok,frame=cap.read()
                if not ok:
                    raise RuntimeError(f"Cannot read frame {frame_id}")
                cached=frame_id
            canvas=cv2.resize(frame,None,fx=scale,fy=scale)
            state="None" if selected is None else "Unavailable"
            for t in rows[frame_id]["tracks"]:
                if t["state"]=="Tentative":continue
                current=t["id"]==selected
                if current:
                    state="Unavailable" if t["identity_ambiguous"] else ("TemporarilyLost" if t["state"]=="Lost" else "Selected")
                l,top,r,b=(round(i*scale) for i in t["xyxy"])
                color=(0,190,255) if current else (80,230,120)
                cv2.rectangle(canvas,(l,top),(r,b),color,2 if current else 1)
                cv2.putText(canvas,f"#{t['id']} {t['state']}",(l,max(top-2,12)),0,.45,color,1)
            cv2.putText(canvas,f"Frame {frame_id} | ID {selected} {state}",(6,20),0,.5,(255,255,255),1)
            cv2.imshow(window,canvas)
            key=cv2.waitKeyEx(40 if not paused else 60)
            if key==27 or cv2.getWindowProperty(window,cv2.WND_PROP_VISIBLE)<1: break
            if key==32: paused=not paused
            if key in (2424832,ord('a')): frame_id=max(0,frame_id-1); paused=True
            elif key in (2555904,ord('d')): frame_id=min(len(rows)-1,frame_id+1); paused=True
            elif not paused:
                frame_id=min(len(rows)-1,frame_id+1)
                if frame_id==len(rows)-1: paused=True
    finally:
        cap.release()
        cv2.destroyAllWindows()


if __name__=="__main__":
    main()
