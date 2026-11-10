"""Run the SA01–SA04 evidence batch and record exit codes.

Exit 0 is not used. Exit 2 means the batch completed and the required device,
firmware and TensorRT results are UNKNOWN. Exit 1 means a host gate, effect
check or tool failed. UNKNOWN is not a pass.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def run(command, cwd):
    completed = subprocess.run(command, cwd=cwd)
    return {"command": " ".join(command), "exit_code": completed.returncode}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.out.exists():
        raise SystemExit("output directory must be new")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()
    if dirty or len(commit) != 40:
        raise SystemExit("refusing to freeze a dirty or unknown commit")
    binary = str(args.binary.resolve())
    out = args.out
    out.mkdir(parents=True)
    results = []
    inventory = out / "SA01" / "inventory"
    suite = out / "SA02-SA04" / "suite"
    audit_out = out / "SA03" / "audit-summary.json"
    probe = out / "SA04" / "model-probe"
    audit_out.parent.mkdir(parents=True)
    results.append(run([binary, "inventory", "--root", str(root), "--out", str(inventory),
                        "--commit", commit, "--dirty", "0"], root))
    results.append(run([binary, "run", "--root", str(root), "--config", "configs/serial/assist.json",
                        "--out", str(suite), "--commit", commit, "--dirty", "0"], root))
    results.append(run([sys.executable, "tools/audit_serial_assist.py", str(suite), "--tree",
                        "--acceptance", "configs/serial/acceptance-profile.json", "--out", str(audit_out)], root))
    results.append(run([sys.executable, "tools/probe_serial_model.py", "--root", str(root), "--out", str(probe)], root))
    failed = [item for item in results if item["exit_code"] not in (0, 2)]
    report = {
        "commit": commit,
        "passed": False,
        "unknown_counted_as_pass": False,
        "overall": "failed" if failed else "UNKNOWN",
        "commands": results,
        "note": "Exit 2 means completed with UNKNOWN device/TensorRT status. It is not a pass. Exit 0 from inventory only means the freeze file was written.",
    }
    (out / "batch-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failed else 2


if __name__ == "__main__":
    sys.exit(main())
