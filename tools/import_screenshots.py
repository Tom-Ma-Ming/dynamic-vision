"""Read-only intake of flat image / YOLO label pairs (requires Pillow)."""
import argparse
import hashlib
import json
import math
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image

EXTENSIONS = {".jpg", ".jpeg", ".png"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_label(line):
    cls, *coords = line.split()
    if cls not in {"0", "1"} or len(coords) != 4:
        raise ValueError("invalid class or field count")
    x, y, w, h = map(float, coords)
    if not all(map(math.isfinite, (x, y, w, h))) or min(w, h) <= 0:
        raise ValueError("invalid box size or coordinates")
    if min(x-w/2, y-h/2, 1-x-w/2, 1-y-h/2) < -0.000001:
        raise ValueError("box outside image")
    return int(cls), x, y, w, h


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--existing-splits", type=Path)
    args = parser.parse_args()
    images = sorted(p for p in args.source.iterdir() if p.suffix.lower() in EXTENSIONS)
    if not images:
        parser.error("no supported images")
    references = defaultdict(set)
    if args.existing_splits:
        for split in ("train", "val", "test"):
            for p in (args.existing_splits / split).iterdir():
                if p.suffix.lower() in EXTENSIONS:
                    references[digest(p)].add(split)
    rows, issues = [], []
    sizes, classes, matches, hashes = Counter(), Counter(), Counter(), Counter()
    empty = 0
    labels = set(args.source.glob("*.txt"))
    for p in images:
        label = p.with_suffix(".txt")
        labels.discard(label)
        sha = digest(p)
        hashes[sha] += 1
        splits = sorted(references.get(sha, ()))
        matches.update(splits)
        errors = []
        width = height = None
        try:
            with Image.open(p) as im:
                im.load()
                width, height = im.size
                sizes[f"{width}x{height}"] += 1
        except OSError as exc:
            errors.append(str(exc))
        boxes = []
        if label.is_file():
            for n, line in enumerate(label.read_text(encoding="utf-8-sig").splitlines(), 1):
                if line.strip():
                    try:
                        boxes.append(parse_label(line))
                    except ValueError as exc:
                        errors.append(f"line {n}: {exc}")
        else:
            errors.append("missing label")
        if len(set(boxes)) != len(boxes):
            errors.append("duplicate box")
        classes.update(b[0] for b in boxes)
        if errors:
            issues.append({"image": p.name, "errors": errors})
        elif not boxes:
            empty += 1
        rows.append(dict(frame_id=p.name, image=str(p.resolve()), label=str(label.resolve()),
                         sha256=sha, width=width, height=height, session_id=None,
                         scene_id=None, capture_ts_ns=None, review_status="draft",
                         valid=not errors, matching_splits=splits))
    report = dict(images=len(images), sizes=sizes, valid_boxes_by_class=classes,
                  empty_labels=empty, invalid_images=len(issues), issues=issues,
                  orphan_labels=sorted(p.name for p in labels),
                  duplicate_image_copies=sum(n-1 for n in hashes.values()),
                  matching_existing_splits=matches,
                  matched_hashes_in_multiple_splits=sum(len(references[h]) > 1 for h in hashes))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "frames.jsonl").write_text(
        "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows), encoding="utf-8")
    (args.output / "quality.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "issues"}))
    return int(bool(issues or labels))


if __name__ == "__main__":
    raise SystemExit(main())
