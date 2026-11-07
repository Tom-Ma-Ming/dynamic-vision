"""Build a reproducible layered evaluation report from existing artifacts.

The tool is read-only with respect to artifacts. It aggregates detection,
TensorRT, MOT, replay and test evidence into report.json / report.md /
report.html and marks missing strata instead of filling them with defaults.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import html
import json
import platform
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 2
TOOL_NAME = "tools/eval_report.py"

GATE_NAMES = [
    ("T01", "无 GPU 或缺少 TensorRT DLL"),
    ("T02", "SDK/Engine 版本不匹配"),
    ("T03", "320 缩放与坐标逆变换"),
    ("T04", "空白/纯负样本"),
    ("T05", "仅露头/远距离小框"),
    ("T06", "相邻帧交叉/遮挡"),
    ("T07", "推理慢于采集时的有界队列"),
    ("T08", "乱序/过期结果"),
    ("T09", "急停/变向/蹲下"),
    ("T10", "Engine 与 ONNX 数值/框对照"),
    ("T11", "1000 帧重复回放"),
    ("T12", "仿真成功/失败/未知"),
]


def load_json(path: Path | None) -> Any:
    if path is None or not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    try:
        data = path.read_bytes()
    except OSError:
        return ""
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig", errors="replace")
    return data.decode("utf-8", errors="replace")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relative(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def file_entry(path: Path, root: Path) -> dict[str, Any]:
    return {
        "path": relative(path, root),
        "sha256": sha256(path),
        "bytes": path.stat().st_size,
    }


def first_existing(paths: list[Path]) -> Path | None:
    return next((path for path in paths if path.exists()), None)


def discover(artifacts: Path) -> dict[str, Any]:
    run_dirs = sorted(path for path in (artifacts / "runs").glob("*") if path.is_dir())
    preferred_run = artifacts / "runs" / "yolo11n-v002-320"
    run = preferred_run if preferred_run.exists() else (run_dirs[0] if run_dirs else None)

    engine_dirs = sorted(
        path for path in (artifacts / "engines").glob("*") if (path / "manifest.json").exists()
    )
    preferred_engine = artifacts / "engines" / "yolo11n-v002-fp32"
    engine = preferred_engine if (preferred_engine / "manifest.json").exists() else (
        engine_dirs[0] if engine_dirs else None
    )

    mot_candidates = sorted((artifacts / "replay").glob("*/synthetic-metrics.json"))
    mot = first_existing(mot_candidates)
    sim = first_existing(sorted(artifacts.glob("sim/*/summary.json")))

    replay_paths: dict[str, Path] = {}
    for path in sorted((artifacts / "replay").glob("**/summary.json")):
        replay_paths[relative(path, artifacts)] = path
    for path in sorted((artifacts / "track02").glob("*-summary.json")):
        replay_paths[relative(path, artifacts)] = path

    quality_candidates = sorted(
        artifacts.glob("validation-*/labeled-review/quality.json")
    ) + sorted(artifacts.glob("data-intake/quality.json"))

    evidence = {
        "cpu_ctest": artifacts / "track02" / "cpu-ctest.log",
        "windows_sdk_ctest": artifacts / "track02" / "windows-sdk-ctest.log",
        "windows_full_ctest": artifacts / "track02" / "windows-full-ctest.log",
        "pytest": artifacts / "track02" / "pytest.log",
        "verify_replay": artifacts / "track02" / "verify-replay.txt",
        "blocking_compare": artifacts / "track02" / "blocking-compare.txt",
        "realtime_invariants": artifacts / "track02" / "realtime-invariants.txt",
        "overload_invariants": artifacts / "track02" / "realtime-overload-invariants.txt",
    }

    return {
        "baseline": run / "baseline.json" if run else None,
        "diagnosis": run / "person-diagnosis.json" if run else None,
        "contract": run / "weights" / "onnx-contract.json" if run else None,
        "engine_manifest": engine / "manifest.json" if engine else None,
        "engine_verification": engine / "verification.json" if engine else None,
        "image_check": engine / "image-check.json" if engine else None,
        "mot_metrics": mot,
        "sim_summary": sim,
        "dataset_summary": artifacts / "datasets" / "yolo_v002" / "summary.json",
        "quality": first_existing(quality_candidates),
        "replay_summaries": [replay_paths[key] for key in sorted(replay_paths)],
        "evidence": evidence,
        "environment": artifacts.parent / "docs" / "environment-lock.md",
    }


def parse_environment(path: Path | None) -> dict[str, str]:
    rows: dict[str, str] = {}
    for line in read_text(path).splitlines():
        match = re.match(r"^\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*$", line)
        if not match:
            continue
        key, value = match.group(1).strip(), match.group(2).strip()
        if key in {"项目", "Item"} or not value.strip("-: "):
            continue
        rows[key] = value
    return rows


def scan_dataset(dataset_root: Path) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for split in ("train", "val"):
        images = len(list((dataset_root / "images" / split).glob("*.jpg")))
        boxes: dict[str, int] = {}
        label_dir = dataset_root / "labels" / split
        for label in sorted(label_dir.glob("*.txt")) if label_dir.exists() else []:
            for line in label.read_text(encoding="utf-8", errors="replace").splitlines():
                fields = line.split()
                if len(fields) >= 5:
                    key = fields[0]
                    boxes[key] = boxes.get(key, 0) + 1
        result[split] = {"images": images, "boxes": boxes}
    return result


def parse_data(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any]:
    summary = load_json(discovered["dataset_summary"])
    if summary:
        data = {
            "source": relative(discovered["dataset_summary"], artifacts),
            "train": summary.get("train", {}),
            "val": summary.get("val", {}),
        }
    else:
        dataset_root = artifacts / "datasets" / "yolo_v002"
        if dataset_root.exists():
            scanned = scan_dataset(dataset_root)
            data = {"source": "scan", "train": scanned.get("train", {}),
                    "val": scanned.get("val", {})}
        else:
            data = {"source": None, "train": {}, "val": {}}

    quality = load_json(discovered["quality"])
    data["negatives"] = {
        "empty_labels": quality.get("empty_labels") if quality else None,
        "source": relative(discovered["quality"], artifacts) if discovered["quality"] else None,
    }
    data["scene_metadata"] = "missing"
    return data


def parse_detection(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any] | None:
    baseline = load_json(discovered["baseline"])
    if not baseline:
        return None
    diagnosis = load_json(discovered["diagnosis"]) or {}
    classes = baseline.get("classes", [])
    person = next((item for item in classes if item.get("Class") == "person"), None)
    head = next((item for item in classes if item.get("Class") == "head"), None)
    operating_points = diagnosis.get("operating_points", [])
    conf25 = next(
        (item for item in operating_points if abs(float(item.get("conf", -1)) - 0.25) < 1e-9),
        None,
    )
    return {
        "source": relative(discovered["baseline"], artifacts),
        "overall": baseline.get("overall", {}),
        "classes": classes,
        "person_recall": person.get("Box-R") if person else None,
        "head_recall": head.get("Box-R") if head else None,
        "person_recall_target": 0.95,
        "person_recall_gap": (
            max(0.0, 0.95 - float(person.get("Box-R")))
            if person and person.get("Box-R") is not None
            else None
        ),
        "conf25": conf25,
        "size_buckets": diagnosis.get("sizes", {}),
        "looser_iou": diagnosis.get("looser_iou"),
        "diagnosis_source": relative(discovered["diagnosis"], artifacts)
        if discovered["diagnosis"]
        else None,
    }


def parse_engine(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any] | None:
    manifest = load_json(discovered["engine_manifest"])
    if not manifest:
        return None
    verification = load_json(discovered["engine_verification"]) or {}
    image_check = load_json(discovered["image_check"]) or []
    samples = verification.get("samples", [])
    max_abs = max((float(item.get("max_abs", 0)) for item in samples), default=None)
    max_px = max((float(item.get("max_px", 0)) for item in image_check), default=None)
    detections = sum(len(item.get("detections", [])) for item in samples)
    return {
        "source": relative(discovered["engine_manifest"], artifacts),
        "manifest": manifest,
        "verification_source": relative(discovered["engine_verification"], artifacts)
        if discovered["engine_verification"]
        else None,
        "verification_samples": len(samples),
        "verification_detections": detections,
        "verification_max_abs": max_abs,
        "image_check_source": relative(discovered["image_check"], artifacts)
        if discovered["image_check"]
        else None,
        "image_check_max_px": max_px,
    }


def parse_mot(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any] | None:
    values = load_json(discovered["mot_metrics"])
    if not values:
        return None
    return {
        "source": relative(discovered["mot_metrics"], artifacts),
        "kind": "synthetic",
        "frames": values.get("frames"),
        "HOTA": values.get("HOTA"),
        "IDF1": values.get("IDF1"),
        "IDSW": values.get("IDSW"),
        "real_identity_truth": "missing",
    }


def parse_sim(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any] | None:
    values = load_json(discovered["sim_summary"])
    if not values:
        return None
    aggregate = values.get("aggregate", {})
    scenarios = values.get("scenarios", [])
    return {
        "source": relative(discovered["sim_summary"], artifacts),
        "scenarios": len(scenarios),
        "success": aggregate.get("success"),
        "failed": aggregate.get("failed"),
        "unknown": aggregate.get("unknown"),
        "success_rate": aggregate.get("success_rate"),
        "all_terminal": aggregate.get("all_terminal"),
        "by_motion": aggregate.get("by_motion", {}),
        "by_size": aggregate.get("by_size", {}),
    }


def parse_replay(discovered: dict[str, Any], artifacts: Path) -> list[dict[str, Any]]:
    rows = []
    seen = set()
    for path in discovered["replay_summaries"]:
        values = load_json(path)
        if not values:
            continue
        signature = json.dumps(values, sort_keys=True)
        if signature in seen:
            continue
        seen.add(signature)
        rows.append(
            {
                "path": relative(path, artifacts),
                "mode": values.get("mode", "legacy"),
                "capacity": values.get("capacity"),
                "paced": values.get("paced"),
                "frames": values.get("frames"),
                "captured": values.get("captured"),
                "processed": values.get("processed", values.get("frames")),
                "replaced": values.get("replaced"),
                "rejected": values.get("rejected"),
                "pending": values.get("pending"),
                "stale": values.get("stale"),
                "failed": values.get("failed"),
                "peak": values.get("peak"),
                "queue_wait_ms": values.get("queue_wait_ms"),
                "infer_ms": values.get("infer_ms"),
                "mot_ms": values.get("mot_ms"),
                "result_age_ms": values.get("result_age_ms"),
                "detect_p50_ms": values.get("detect_p50_ms"),
                "detect_p95_ms": values.get("detect_p95_ms"),
                "mot_p50_ms": values.get("mot_p50_ms"),
                "mot_p95_ms": values.get("mot_p95_ms"),
            }
        )
    return rows


def parse_ctest(path: Path, artifacts: Path) -> dict[str, Any] | None:
    text = read_text(path)
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
    if not match:
        return {"path": relative(path, artifacts), "status": "missing"}
    percent, failed, total = (int(value) for value in match.groups())
    return {
        "path": relative(path, artifacts),
        "status": "passed" if percent == 100 and failed == 0 else "failed",
        "passed": total - failed,
        "failed": failed,
        "total": total,
    }


def parse_pytest(path: Path, artifacts: Path) -> dict[str, Any] | None:
    text = read_text(path)
    match = re.search(r"(\d+) passed", text)
    if not match:
        return {"path": relative(path, artifacts), "status": "missing"}
    return {
        "path": relative(path, artifacts),
        "status": "passed",
        "passed": int(match.group(1)),
    }


def parse_evidence(discovered: dict[str, Any], artifacts: Path) -> dict[str, Any]:
    paths = discovered["evidence"]
    result: dict[str, Any] = {
        "cpu_ctest": parse_ctest(paths["cpu_ctest"], artifacts),
        "windows_sdk_ctest": parse_ctest(paths["windows_sdk_ctest"], artifacts),
        "windows_full_ctest": parse_ctest(paths["windows_full_ctest"], artifacts),
        "pytest": parse_pytest(paths["pytest"], artifacts),
    }

    verify_text = read_text(paths["verify_replay"])
    verify_match = re.search(r"Verified two 1000-frame runs: ([0-9a-f]+)", verify_text)
    result["verify_replay"] = {
        "path": relative(paths["verify_replay"], artifacts),
        "status": "passed" if verify_match else "missing",
        "hash": verify_match.group(1) if verify_match else None,
    }

    blocking_text = read_text(paths["blocking_compare"])
    blocking_match = re.search(r"stable_equal=(True|False)", blocking_text)
    result["blocking_compare"] = {
        "path": relative(paths["blocking_compare"], artifacts),
        "status": "passed" if blocking_match and blocking_match.group(1) == "True" else "missing",
        "stable_equal": blocking_match.group(1) if blocking_match else None,
    }

    for key, path in (
        ("realtime_invariants", paths["realtime_invariants"]),
        ("overload_invariants", paths["overload_invariants"]),
    ):
        values: dict[str, str] = {}
        for line in read_text(path).splitlines():
            if "=" in line:
                name, value = line.split("=", 1)
                values[name.strip()] = value.strip()
        result[key] = {
            "path": relative(path, artifacts),
            "status": "present" if values else "missing",
            "values": values,
        }
    return result


def build_gates(
    data: dict[str, Any],
    detection: dict[str, Any] | None,
    engine: dict[str, Any] | None,
    mot: dict[str, Any] | None,
    replay: list[dict[str, Any]],
    evidence: dict[str, Any],
    sim: dict[str, Any] | None,
) -> list[dict[str, str]]:
    def status(condition: bool, missing: bool = False) -> str:
        if missing:
            return "missing"
        return "pass" if condition else "partial"

    cpu = evidence.get("cpu_ctest") or {}
    verify = evidence.get("verify_replay") or {}
    overload = (evidence.get("overload_invariants") or {}).get("values", {})
    t03 = bool(engine and engine.get("image_check_max_px") is not None
               and engine["image_check_max_px"] < 1.0)
    t04 = bool((data.get("negatives") or {}).get("empty_labels") or 0) > 0
    t07 = (
        overload.get("conservation") == "True"
        and int(overload.get("processed", -1)) + int(overload.get("replaced", -1)) == 1000
        and int(overload.get("peak", 99)) <= 1
    )
    t10 = bool(
        engine
        and engine.get("verification_max_abs") is not None
        and engine["verification_max_abs"] <= 0.01
        and engine.get("image_check_max_px") is not None
        and engine["image_check_max_px"] < 1.0
    )
    t11 = verify.get("status") == "passed" and bool(replay)
    t12 = bool(
        sim
        and sim.get("all_terminal")
        and (sim.get("success") or 0) > 0
        and (sim.get("failed") or 0) > 0
        and (sim.get("unknown") or 0) > 0
    )
    statuses = {
        "T01": ("partial", "env_probe 无 TRT 时返回 2；未做 DLL 缺失注入"),
        "T02": ("partial", "runner 检查 getInferLibVersion 与头文件版本；未做错版 Engine 注入"),
        "T03": ("pass" if t03 else "missing", "engine/image-check.json 最大像素误差 < 1"),
        "T04": ("pass" if t04 else "missing", "labeled-review quality.json 的 empty_labels"),
        "T05": ("partial", "head 指标存在；仅有露头分桶未单独列出"),
        "T06": ("pass" if mot else "missing", "mot-v1 synthetic-metrics.json（合成序列）"),
        "T07": ("pass" if t07 else "missing", "500 FPS 过载 invariants + BoundedQueue 用例"),
        "T08": ("pass" if cpu.get("status") == "passed" else "missing", "mot_synthetic 过期结果断言"),
        "T09": ("missing", "没有急停/变向/蹲下真值录像"),
        "T10": ("pass" if t10 else "missing", "engine/verification.json 原始张量与框对照"),
        "T11": ("pass" if t11 else "missing", "verify-replay.txt 双跑哈希"),
        "T12": ("pass" if t12 else ("partial" if sim else "missing"),
                "sim summary：SUCCESS/FAILED/UNKNOWN 均出现且全部终止"),
    }
    gates = []
    for gate_id, name in GATE_NAMES:
        gate_status, detail = statuses[gate_id]
        gates.append({"id": gate_id, "name": name, "status": gate_status, "evidence": detail})
    return gates


def build_gaps(
    data: dict[str, Any],
    detection: dict[str, Any] | None,
    mot: dict[str, Any] | None,
    replay: list[dict[str, Any]],
) -> list[dict[str, str]]:
    gaps: list[dict[str, str]] = []
    person_recall = detection.get("person_recall") if detection else None
    if person_recall is not None and person_recall < 0.95:
        gaps.append(
            {
                "id": "person-recall",
                "severity": "high",
                "detail": f"person Box-R={person_recall:.4f}，目标 0.95；未达标",
            }
        )
    if not ((data.get("negatives") or {}).get("empty_labels") or 0):
        gaps.append(
            {
                "id": "negative-samples",
                "severity": "high",
                "detail": "没有空标签/纯负样本，误报率无法评估",
            }
        )
    gaps.append(
        {
            "id": "scene-strata",
            "severity": "high",
            "detail": "数据集缺少 scene_id/场次元数据，无法按场景分层",
        }
    )
    if mot and mot.get("kind") == "synthetic":
        gaps.append(
            {
                "id": "real-mot-truth",
                "severity": "high",
                "detail": "MOT 指标来自 36 帧合成序列，没有真实录像身份真值",
            }
        )
    max_frames = max((row.get("frames") or row.get("processed") or 0 for row in replay), default=0)
    if max_frames < 5000:
        gaps.append(
            {
                "id": "long-run",
                "severity": "medium",
                "detail": f"最长回放 {max_frames} 帧；尚无 2 小时/长时资源趋势",
            }
        )
    gaps.append(
        {
            "id": "remote-ci",
            "severity": "medium",
            "detail": "GitHub Actions 未实际运行；本机日志不能证明远程 CI",
        }
    )
    gaps.append(
        {
            "id": "camera-motion-imm",
            "severity": "medium",
            "detail": "IMM、镜头运动补偿、ByteTrack/ReID 尚未实现与消融",
        }
    )
    return gaps


def build_report(artifacts: Path, sim_summary: Path | None = None) -> dict[str, Any]:
    discovered = discover(artifacts)
    if sim_summary is not None:
        if load_json(sim_summary) is None:
            raise ValueError(f"Invalid simulation summary: {sim_summary}")
        discovered["sim_summary"] = sim_summary
    environment = parse_environment(discovered["environment"])
    data = parse_data(discovered, artifacts)
    detection = parse_detection(discovered, artifacts)
    engine = parse_engine(discovered, artifacts)
    mot = parse_mot(discovered, artifacts)
    sim = parse_sim(discovered, artifacts)
    replay = parse_replay(discovered, artifacts)
    evidence = parse_evidence(discovered, artifacts)
    gates = build_gates(data, detection, engine, mot, replay, evidence, sim)
    gaps = build_gaps(data, detection, mot, replay)

    required_paths = {
        "detection.baseline": discovered["baseline"],
        "engine.manifest": discovered["engine_manifest"],
        "mot.synthetic": discovered["mot_metrics"],
        "replay.track02": next(
            (path for path in discovered["replay_summaries"]
             if relative(path, artifacts).startswith("track02/")),
            None,
        ),
        "evidence.verify_replay": discovered["evidence"]["verify_replay"],
        "evidence.cpu_ctest": discovered["evidence"]["cpu_ctest"],
        "evidence.pytest": discovered["evidence"]["pytest"],
    }
    missing_required = [
        key for key, path in required_paths.items() if path is None or not Path(path).exists()
    ]

    inputs = []
    seen = set()
    for path in [
        discovered["baseline"],
        discovered["diagnosis"],
        discovered["contract"],
        discovered["engine_manifest"],
        discovered["engine_verification"],
        discovered["image_check"],
        discovered["mot_metrics"],
        discovered["sim_summary"],
        discovered["dataset_summary"],
        discovered["quality"],
        discovered["environment"],
        *discovered["replay_summaries"],
        *discovered["evidence"].values(),
    ]:
        if path is not None and Path(path).exists():
            resolved = Path(path).resolve()
            if resolved not in seen:
                seen.add(resolved)
                inputs.append(file_entry(Path(path), artifacts))

    gate_counts = {
        "pass": sum(1 for gate in gates if gate["status"] == "pass"),
        "partial": sum(1 for gate in gates if gate["status"] == "partial"),
        "missing": sum(1 for gate in gates if gate["status"] == "missing"),
    }
    summary = {
        "status": "complete-with-gaps" if not missing_required else "incomplete",
        "gates": gate_counts,
        "person_recall": detection.get("person_recall") if detection else None,
        "person_recall_target": 0.95,
        "mot_hota": mot.get("HOTA") if mot else None,
        "mot_idf1": mot.get("IDF1") if mot else None,
        "mot_idsw": mot.get("IDSW") if mot else None,
        "sim_success_rate": sim.get("success_rate") if sim else None,
        "replays": len(replay),
        "gaps": len(gaps),
    }
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "tool": TOOL_NAME,
        "python": platform.python_version(),
        "platform": platform.platform(),
        "artifacts_root": str(artifacts),
        "environment": environment,
        "data": data,
        "detection": detection,
        "engine": engine,
        "mot": mot,
        "sim": sim,
        "replay": replay,
        "evidence": evidence,
        "gates": gates,
        "gaps": gaps,
        "summary": summary,
        "missing_required": missing_required,
        "inputs": inputs,
    }


def fmt(value: Any, digits: int = 6) -> str:
    if value is None:
        return "missing"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return f"{value:.{digits}g}"
    return str(value)


def boxes_text(boxes: Any) -> str:
    if not isinstance(boxes, dict):
        return fmt(boxes)
    return ", ".join(f"{key}={value}" for key, value in boxes.items())


def md_table(headers: list[str], rows: list[list[Any]]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |",
             "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(fmt(cell) for cell in row) + " |")
    return lines


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# 动态视觉工程评估报告",
        "",
        f"- 生成时间：{report['generated_at']}",
        f"- artifacts：`{report['artifacts_root']}`",
        f"- 状态：{report['summary']['status']}",
        "",
        "## 摘要",
        "",
    ]
    lines += md_table(
        ["指标", "值"],
        [
            ["通过/部分/缺失 gate", f"{report['summary']['gates']['pass']} / "
                                  f"{report['summary']['gates']['partial']} / "
                                  f"{report['summary']['gates']['missing']}"],
            ["person recall / 目标", f"{fmt(report['summary']['person_recall'])} / 0.95"],
            ["MOT HOTA / IDF1 / IDSW", f"{fmt(report['summary']['mot_hota'])} / "
                                      f"{fmt(report['summary']['mot_idf1'])} / "
                                      f"{fmt(report['summary']['mot_idsw'])}"],
            ["仿真成功率", fmt(report["summary"]["sim_success_rate"])],
            ["回放报告数", report["summary"]["replays"]],
            ["缺口数", report["summary"]["gaps"]],
        ],
    )
    lines += ["", "## 环境", ""]
    if report["environment"]:
        lines += md_table(["项目", "实测"], list(report["environment"].items()))
    else:
        lines.append("environment-lock.md 缺失。")

    lines += ["", "## 数据", ""]
    for split in ("train", "val"):
        value = report["data"].get(split) or {}
        lines.append(f"- {split}: images={fmt(value.get('images'))}, boxes={boxes_text(value.get('boxes'))}")
    lines.append(f"- 负样本/空标签：{fmt((report['data'].get('negatives') or {}).get('empty_labels'))}")
    lines.append(f"- 场景元数据：{report['data'].get('scene_metadata')}")

    lines += ["", "## 检测", ""]
    if report["detection"]:
        overall = report["detection"]["overall"]
        lines += md_table(
            ["指标", "值"],
            [[key, fmt(value)] for key, value in overall.items()],
        )
        lines += ["", "| 类别 | P | R | mAP50 | mAP50-95 |", "|---|---|---|---|---|"]
        for item in report["detection"]["classes"]:
            lines.append(
                f"| {item.get('Class')} | {fmt(item.get('Box-P'))} | {fmt(item.get('Box-R'))} | "
                f"{fmt(item.get('mAP50'))} | {fmt(item.get('mAP50-95'))} |"
            )
        conf25 = report["detection"].get("conf25")
        if conf25:
            lines += ["", "conf 0.25 / IoU 0.5 操作点：", ""]
            lines += md_table(
                ["TP", "FP", "FN", "Recall", "Precision"],
                [[conf25.get("tp"), conf25.get("fp"), conf25.get("fn"),
                  fmt(conf25.get("recall")), fmt(conf25.get("precision"))]],
            )
            size = conf25.get("size") or {}
            lines += ["", "尺寸桶：", ""]
            lines += md_table(
                ["桶", "总数", "漏检"],
                [["<32", size.get("under32_total"), size.get("under32_missed")],
                 [">=32", size.get("32plus_total"), size.get("32plus_missed")]],
            )
    else:
        lines.append("检测基线缺失。")

    lines += ["", "## Engine", ""]
    if report["engine"]:
        engine = report["engine"]
        lines += md_table(
            ["项", "值"],
            [["manifest", engine["source"]],
             ["GPU", (engine["manifest"].get("gpu") if engine["manifest"] else None)],
             ["verification 样本/框", f"{engine['verification_samples']} / {engine['verification_detections']}"],
             ["原始张量 max_abs", fmt(engine["verification_max_abs"])],
             ["image-check max_px", fmt(engine["image_check_max_px"])]],
        )
    else:
        lines.append("Engine manifest 缺失。")

    lines += ["", "## MOT", ""]
    if report["mot"]:
        lines += md_table(
            ["项", "值"],
            [["来源", report["mot"]["source"]],
             ["类型", report["mot"]["kind"]],
             ["frames", report["mot"]["frames"]],
             ["HOTA", fmt(report["mot"]["HOTA"])],
             ["IDF1", fmt(report["mot"]["IDF1"])],
             ["IDSW", report["mot"]["IDSW"]],
             ["实景身份真值", report["mot"]["real_identity_truth"]]],
        )
    else:
        lines.append("MOT 指标缺失。")

    lines += ["", "## 仿真闭环", ""]
    if report.get("sim"):
        sim = report["sim"]
        lines += md_table(
            ["项", "值"],
            [["来源", sim["source"]],
             ["场景数", sim["scenarios"]],
             ["SUCCESS / FAILED / UNKNOWN",
              f"{sim['success']} / {sim['failed']} / {sim['unknown']}"],
             ["成功率", fmt(sim["success_rate"])],
             ["全部终止", fmt(sim["all_terminal"])]],
        )
    else:
        lines.append("仿真 summary 缺失。")

    lines += ["", "## 回放与延迟", ""]
    if report["replay"]:
        lines += md_table(
            ["路径", "mode", "cap", "processed", "replaced", "peak",
             "age P50", "age P95", "age P99"],
            [[row["path"], row["mode"], row["capacity"], row["processed"],
              row["replaced"], row["peak"],
              fmt((row["result_age_ms"] or {}).get("p50")),
              fmt((row["result_age_ms"] or {}).get("p95")),
              fmt((row["result_age_ms"] or {}).get("p99"))]
             for row in report["replay"]],
        )
    else:
        lines.append("回放 summary 缺失。")

    lines += ["", "## 证据", ""]
    evidence_rows = []
    for key in ("cpu_ctest", "windows_sdk_ctest", "windows_full_ctest", "pytest",
                "verify_replay", "blocking_compare"):
        item = report["evidence"].get(key) or {}
        result = item.get("passed", item.get("hash", item.get("stable_equal", "")))
        evidence_rows.append([key, item.get("status"), item.get("path"), result])
    lines += md_table(["项", "状态", "路径", "结果"], evidence_rows)

    lines += ["", "## 验收矩阵", ""]
    lines += md_table(
        ["ID", "项", "状态", "证据"],
        [[gate["id"], gate["name"], gate["status"], gate["evidence"]] for gate in report["gates"]],
    )

    lines += ["", "## 缺口", ""]
    lines += md_table(
        ["ID", "严重度", "说明"],
        [[gap["id"], gap["severity"], gap["detail"]] for gap in report["gaps"]],
    )

    lines += ["", "## 输入文件", ""]
    lines += md_table(
        ["路径", "SHA-256", "bytes"],
        [[entry["path"], entry["sha256"], entry["bytes"]] for entry in report["inputs"]],
    )
    return "\n".join(lines) + "\n"


def render_html(report: dict[str, Any]) -> str:
    def table(headers: list[str], rows: list[list[Any]]) -> str:
        head = "".join(f"<th>{html.escape(str(item))}</th>" for item in headers)
        body = "".join(
            "<tr>" + "".join(f"<td>{html.escape(fmt(cell))}</td>" for cell in row) + "</tr>"
            for row in rows
        )
        return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"

    parts = [
        "<!doctype html><html><head><meta charset='utf-8'>",
        "<title>动态视觉工程评估报告</title>",
        "<style>body{font-family:system-ui,sans-serif;margin:2rem;color:#222}"
        "table{border-collapse:collapse;margin:1rem 0;width:100%}"
        "th,td{border:1px solid #ccc;padding:.35rem .5rem;text-align:left;vertical-align:top}"
        "th{background:#f2f2f2}code{background:#f6f6f6;padding:.1rem .25rem}"
        ".pass{color:#146c2e}.partial{color:#8a5a00}.missing{color:#a00}</style></head><body>",
        "<h1>动态视觉工程评估报告</h1>",
        f"<p>生成时间：{html.escape(report['generated_at'])}<br>"
        f"artifacts：<code>{html.escape(report['artifacts_root'])}</code><br>"
        f"状态：<strong>{html.escape(report['summary']['status'])}</strong></p>",
        "<h2>摘要</h2>",
        table(["指标", "值"], [
            ["通过/部分/缺失 gate", f"{report['summary']['gates']['pass']} / "
                                  f"{report['summary']['gates']['partial']} / "
                                  f"{report['summary']['gates']['missing']}"],
            ["person recall / 目标", f"{fmt(report['summary']['person_recall'])} / 0.95"],
            ["MOT HOTA / IDF1 / IDSW", f"{fmt(report['summary']['mot_hota'])} / "
                                      f"{fmt(report['summary']['mot_idf1'])} / "
                                      f"{fmt(report['summary']['mot_idsw'])}"],
        ]),
        "<h2>环境</h2>",
        table(["项目", "实测"], list(report["environment"].items()))
        if report["environment"] else "<p class='missing'>environment-lock.md 缺失。</p>",
        "<h2>数据</h2>",
        table(["split", "images", "boxes"], [
            [split, (report["data"].get(split) or {}).get("images"),
             boxes_text((report["data"].get(split) or {}).get("boxes"))]
            for split in ("train", "val")
        ]),
        "<h2>检测</h2>",
    ]
    if report["detection"]:
        parts.append(table(["指标", "值"], list(report["detection"]["overall"].items())))
        parts.append(table(
            ["类别", "P", "R", "mAP50", "mAP50-95"],
            [[item.get("Class"), item.get("Box-P"), item.get("Box-R"),
              item.get("mAP50"), item.get("mAP50-95")]
             for item in report["detection"]["classes"]],
        ))
    else:
        parts.append("<p class='missing'>检测基线缺失。</p>")

    parts += ["<h2>Engine</h2>"]
    if report["engine"]:
        parts.append(table(["项", "值"], [
            ["manifest", report["engine"]["source"]],
            ["GPU", (report["engine"]["manifest"] or {}).get("gpu")],
            ["verification 样本/框",
             f"{report['engine']['verification_samples']} / "
             f"{report['engine']['verification_detections']}"],
            ["原始张量 max_abs", report["engine"]["verification_max_abs"]],
            ["image-check max_px", report["engine"]["image_check_max_px"]],
        ]))
    else:
        parts.append("<p class='missing'>Engine manifest 缺失。</p>")

    parts += ["<h2>MOT</h2>"]
    if report["mot"]:
        parts.append(table(["项", "值"], [
            ["来源", report["mot"]["source"]],
            ["类型", report["mot"]["kind"]],
            ["HOTA", report["mot"]["HOTA"]],
            ["IDF1", report["mot"]["IDF1"]],
            ["IDSW", report["mot"]["IDSW"]],
        ]))
    else:
        parts.append("<p class='missing'>MOT 指标缺失。</p>")

    parts += ["<h2>仿真闭环</h2>"]
    if report.get("sim"):
        sim = report["sim"]
        parts.append(table(["项", "值"], [
            ["来源", sim["source"]],
            ["场景数", sim["scenarios"]],
            ["SUCCESS / FAILED / UNKNOWN",
             f"{sim['success']} / {sim['failed']} / {sim['unknown']}"],
            ["成功率", sim["success_rate"]],
            ["全部终止", sim["all_terminal"]],
        ]))
    else:
        parts.append("<p class='missing'>仿真 summary 缺失。</p>")

    parts += ["<h2>回放与延迟</h2>"]
    parts.append(table(
        ["路径", "mode", "cap", "processed", "replaced", "peak", "age P50", "age P95", "age P99"],
        [[row["path"], row["mode"], row["capacity"], row["processed"], row["replaced"],
          row["peak"], fmt((row["result_age_ms"] or {}).get("p50")),
          fmt((row["result_age_ms"] or {}).get("p95")),
          fmt((row["result_age_ms"] or {}).get("p99"))] for row in report["replay"]],
    ))

    parts += ["<h2>证据</h2>"]
    parts.append(table(
        ["项", "状态", "路径", "结果"],
        [[key,
          (report["evidence"].get(key) or {}).get("status"),
          (report["evidence"].get(key) or {}).get("path"),
          (report["evidence"].get(key) or {}).get("passed",
              (report["evidence"].get(key) or {}).get("hash", ""))]
         for key in ("cpu_ctest", "windows_sdk_ctest", "windows_full_ctest",
                     "pytest", "verify_replay", "blocking_compare")],
    ))

    parts += ["<h2>验收矩阵</h2>"]
    parts.append(table(
        ["ID", "项", "状态", "证据"],
        [[gate["id"], gate["name"], gate["status"], gate["evidence"]]
         for gate in report["gates"]],
    ))

    parts += ["<h2>缺口</h2>"]
    parts.append(table(
        ["ID", "严重度", "说明"],
        [[gap["id"], gap["severity"], gap["detail"]] for gap in report["gaps"]],
    ))
    parts += ["<h2>输入文件</h2>"]
    parts.append(table(
        ["路径", "SHA-256", "bytes"],
        [[entry["path"], entry["sha256"], entry["bytes"]] for entry in report["inputs"]],
    ))
    parts.append("</body></html>")
    return "".join(parts)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="?", type=Path, default=Path("artifacts"),
                        help="artifacts root (default: artifacts)")
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    parser.add_argument("--strict", action="store_true",
                        help="return 1 when required evidence is missing")
    parser.add_argument("--sim-summary", type=Path, help="Explicit simulation evidence for this report")
    args = parser.parse_args(argv)

    artifacts = args.artifacts.resolve()
    if not artifacts.exists():
        parser.error(f"artifacts root does not exist: {artifacts}")
    if args.out.exists() and any(args.out.iterdir()):
        parser.error(f"output directory must be new or empty: {args.out}")
    args.out.mkdir(parents=True, exist_ok=True)

    report = build_report(artifacts, args.sim_summary)
    (args.out / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    (args.out / "report.md").write_text(render_markdown(report), encoding="utf-8")
    (args.out / "report.html").write_text(render_html(report), encoding="utf-8")
    print(json.dumps({
        "status": report["summary"]["status"],
        "missing_required": report["missing_required"],
        "gates": report["summary"]["gates"],
        "out": str(args.out),
    }, ensure_ascii=False))
    if args.strict and report["missing_required"]:
        for item in report["missing_required"]:
            print(f"missing required evidence: {item}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
