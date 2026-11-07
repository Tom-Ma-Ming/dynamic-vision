#!/usr/bin/env python3
"""边界扫描：检测源码中是否出现输入注入、屏幕抓取、进程注入类符号。

用法：check_boundary.py <root>
退出码 0 表示干净；1 表示有违规，逐行打印 `文件:行:符号`。
"""
import fnmatch
import re
import sys
from pathlib import Path

EXTENSIONS = {".cpp", ".hpp", ".py", ".ps1", ".cmake"}
FORBIDDEN = [
    "SendInput", "mouse_event", "keybd_event", "SetWindowsHookEx",
    "IDXGIOutputDuplication", "DuplicateOutput", "BitBlt",
    "WriteProcessMemory", "CreateRemoteThread", "pyautogui", "pynput",
]
# 相对 root 的目录前缀（含通配）与文件；本脚本和其测试以字面量含有禁用符号。
SKIP_DIRS = ["build*", "tests/tools", ".superpowers", ".git", ".venv", "out",
             "artifacts", "__pycache__"]
SKIP_FILES = ["tools/check_boundary.py"]
PATTERN = re.compile("|".join(re.escape(s) for s in FORBIDDEN))


def skipped(rel: Path) -> bool:
    if rel.as_posix() in SKIP_FILES:
        return True
    parts = rel.parts[:-1]
    for i in range(len(parts)):
        prefix = "/".join(parts[: i + 1])
        if any(fnmatch.fnmatch(prefix, pat) for pat in SKIP_DIRS):
            return True
    return False


def scan(root: Path):
    hits = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in EXTENSIONS:
            continue
        rel = path.relative_to(root)
        if skipped(rel):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for no, line in enumerate(text.splitlines(), 1):
            for m in PATTERN.finditer(line):
                hits.append(f"{rel.as_posix()}:{no}:{m.group(0)}")
    return hits


def main(argv):
    if len(argv) != 2:
        print("用法：check_boundary.py <root>", file=sys.stderr)
        return 2
    hits = scan(Path(argv[1]))
    for h in hits:
        print(h)
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
