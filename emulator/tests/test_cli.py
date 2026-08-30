"""CLI contract tests for zxem (cli-design)."""

from __future__ import annotations

import subprocess
from pathlib import Path

EMULATOR_DIR = Path(__file__).resolve().parents[1]
ZXEM = EMULATOR_DIR / "zxem"


def run(args: list[str], *, timeout: float = 10.0) -> subprocess.CompletedProcess[str]:
    """Run zxem with cwd=emulator/."""
    return subprocess.run(
        [str(ZXEM), *args],
        cwd=EMULATOR_DIR,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def test_no_args_prints_usage_exit_0() -> None:
    p = run([])
    assert p.returncode == 0
    assert "Usage:" in p.stderr
    assert "-h, --help" in p.stderr
    assert "-v, --version" in p.stderr


def test_help_flags() -> None:
    for flag in ("-h", "--help"):
        p = run([flag])
        assert p.returncode == 0, flag
        assert "Usage:" in p.stderr


def test_version_flags() -> None:
    for flag in ("-v", "--version"):
        p = run([flag])
        assert p.returncode == 0, flag
        assert "zxem 0.4" in p.stdout
        assert "verbose" not in p.stdout.lower() or "0.2" in p.stdout


def test_unknown_option_nonzero() -> None:
    p = run(["--not-a-real-flag"])
    assert p.returncode != 0
    assert "Unknown option" in p.stderr


def test_order_independent_help() -> None:
    p = run(["--headless", "-h"])
    assert p.returncode == 0
    assert "Usage:" in p.stderr


def test_help_lists_system_rom_and_disk_flags() -> None:
    p = run(["-h"])
    assert p.returncode == 0
    assert "--no-system-rom" in p.stderr
    assert "--trdos-rom" in p.stderr
    assert "--plus3-rom" in p.stderr
    assert "plus3" in p.stderr
