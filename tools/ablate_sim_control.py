"""Compare two tracker configs using identical simulation observations across three seeds."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from verify_sim_metrics import verify


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("runner","scenarios","baseline","candidate","output"):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    groups=[]
    for offset in (0,100,200):
        text=args.scenarios.read_text(encoding="utf-8-sig")
        text=re.sub(r"seed: (\d+)",lambda m:f"seed: {int(m[1])+offset}",text)
        config=args.output/f"seed-{offset}.yml"
        config.write_text(text)
        pair={}
        for name,tracker in (("baseline",args.baseline),("candidate",args.candidate)):
            dest=args.output/f"{name}-{offset}"
            subprocess.run([str(args.runner),str(config),str(dest),str(tracker)],check=True)
            pair[name]=verify(dest)
            summary=json.loads((dest/"summary.json").read_text())
            for scene in summary["scenarios"]:
                groups.append({"seed_offset":offset,"method":name,"scene":scene["id"],
                               "result":scene["result"],**scene["metrics"]})
        for scene in pair["baseline"]:
            a,b=pair["baseline"][scene],pair["candidate"][scene]
            assert len(a)==len(b)
            for x,y in zip(a,b):
                for key in ("truth_x","truth_y","observed","detections"):
                    assert x[key]==y[key]
    (args.output/"comparison.json").write_text(json.dumps(groups,indent=2))
    for scene in ("constant","stop","turn","small","low_conf"):
        print(scene)
        for method in ("baseline","candidate"):
            rows=[r for r in groups if r["scene"]==scene and r["method"]==method]
            print(method,"control MAE",round(sum(r["control_mae_px"] for r in rows)/len(rows),3),
                  "coverage",round(sum(r["track_coverage"] for r in rows)/len(rows),3))


if __name__=="__main__":
    main()
