"""CLI contract tests for zxem (cli-design)."""

from __future__ import annotations

import os
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
        assert "zxem 0.7" in p.stdout
        assert "verbose" not in p.stdout.lower()


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
    assert "--keymap" in p.stderr
    assert "CLI flags" in p.stderr
    assert "compiled defaults" in p.stderr


def _tiny_tap() -> bytes:
    """Minimal TAP: CODE header + 1-byte RET payload (same layout as Catch2)."""
    hdr_body = bytes(
        [
            0x00,
            3,
            *b"TEST      ",
            1,
            0,
            0x00,
            0x80,
            0x00,
            0x80,
        ]
    )
    hx = 0
    for b in hdr_body:
        hx ^= b
    header = bytes([19, 0]) + hdr_body + bytes([hx])
    data_body = bytes([0xFF, 0xC9])
    dx = 0
    for b in data_body:
        dx ^= b
    data = bytes([3, 0]) + data_body + bytes([dx])
    return header + data


def test_cli_keymap_wins_over_ini_regardless_of_flag_order(tmp_path: Path) -> None:
    """CLI overrides INI even when --config appears after --keymap (AUDIT FE-3)."""
    tap = tmp_path / "t.tap"
    tap.write_bytes(_tiny_tap())
    ini = tmp_path / "zxem.ini"
    ini.write_text("[input]\nkeymap = wasd\n", encoding="utf-8")

    env = os.environ.copy()
    lsan = env.get("LSAN_OPTIONS", "")
    extra = "exitcode=0"
    env["LSAN_OPTIONS"] = f"{lsan}:{extra}" if lsan else extra

    orders = (
        ["--keymap", "spectrum", "--config", str(ini)],
        ["--config", str(ini), "--keymap", "spectrum"],
    )
    for prefix in orders:
        p = subprocess.run(
            [
                str(ZXEM),
                *prefix,
                "--headless",
                "--frames",
                "1",
                "--no-audio",
                str(tap),
            ],
            cwd=EMULATOR_DIR,
            capture_output=True,
            text=True,
            timeout=30.0,
            check=False,
            env=env,
        )
        assert p.returncode == 0, p.stderr
        assert "keymap=spectrum" in p.stderr
        assert "keymap=wasd" not in p.stderr
