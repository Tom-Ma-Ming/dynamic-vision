import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "check_boundary.py"


def run(root):
    return subprocess.run([sys.executable, str(SCRIPT), str(root)],
                          capture_output=True, text=True)


def test_forbidden_symbol_fails(tmp_path):
    (tmp_path / "bad.cpp").write_text("void f() {\n  SendInput(1, nullptr, 0);\n}\n")
    r = run(tmp_path)
    assert r.returncode == 1
    assert "bad.cpp:2:SendInput" in r.stdout


def test_clean_tree_passes(tmp_path):
    (tmp_path / "ok.cpp").write_text("int main() { return 0; }\n")
    assert run(tmp_path).returncode == 0


def test_skipped_dirs_ignored(tmp_path):
    for d in ("build", "build-win", "tests/tools", ".superpowers"):
        p = tmp_path / d
        p.mkdir(parents=True)
        (p / "x.py").write_text("import pyautogui\n")
    assert run(tmp_path).returncode == 0


def test_other_extensions_scanned(tmp_path):
    (tmp_path / "a.ps1").write_text("# x\nWriteProcessMemory\n")
    (tmp_path / "b.cmake").write_text("BitBlt\n")
    r = run(tmp_path)
    assert r.returncode == 1
    assert "a.ps1:2:WriteProcessMemory" in r.stdout
    assert "b.cmake:1:BitBlt" in r.stdout
