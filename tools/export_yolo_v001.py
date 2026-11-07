"""Export flat YOLO pairs. Train source: person/head; val source: head/person."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory

from import_screenshots import EXTENSIONS, digest, parse_label


def export(train, val, output):
    output = output.resolve()
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    seen = {}
    manifest = []
    report = {}
    with TemporaryDirectory(dir=output.parent) as temp:
        root = Path(temp) / "dataset"
        for split, source in (("train", train), ("val", val)):
            images = sorted(p for p in source.iterdir() if p.suffix.lower() in EXTENSIONS)
            if not images or len({p.stem for p in images}) != len(images):
                raise ValueError(f"{source}: missing images or conflicting stems")
            image_dir = root / "images" / split
            label_dir = root / "labels" / split
            image_dir.mkdir(parents=True)
            label_dir.mkdir(parents=True)
            counts = Counter()
            for image in images:
                sha = digest(image)
                if sha in seen:
                    raise ValueError(f"Repeated image: {image} and {seen[sha]}")
                seen[sha] = image
                label = image.with_suffix(".txt")
                lines = []
                for line in label.read_text(encoding="utf-8-sig").splitlines():
                    if not line.strip():
                        continue
                    cls, *_ = parse_label(line)
                    new_id = 1 - cls if split == "train" else cls
                    counts[("head", "person")[new_id]] += 1
                    lines.append(f"{new_id} {line.split(maxsplit=1)[1]}\n")
                target = image_dir / image.name
                shutil.copyfile(image, target)
                (label_dir / label.name).write_text("".join(lines), encoding="utf-8")
                manifest.append(dict(split=split, image=str(image.resolve()), sha256=sha,
                                     source_label_sha256=digest(label)))
            report[split] = dict(images=len(images), boxes=counts)
        (root / "dataset.yaml").write_text(
            f"path: {json.dumps(output.as_posix(), ensure_ascii=False)}\n"
            "train: images/train\nval: images/val\nnames:\n  0: head\n  1: person\n", encoding="utf-8")
        (root / "manifest.jsonl").write_text(
            "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in manifest), encoding="utf-8")
        (root / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        root.rename(output)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("train", "val", "output"):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    print(export(args.train, args.val, args.output))
