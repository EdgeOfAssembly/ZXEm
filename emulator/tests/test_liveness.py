"""Headless liveness heartbeat (AUDIT TEST-2).

Pass must not be "process still alive". A 48K SNA with IFF1=1, IM 1, HALT at
0x8000 must leave PC=0x8001 once maskable INT is delivered.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

EMULATOR_DIR = Path(__file__).resolve().parents[1]
ZXEM = EMULATOR_DIR / "zxem"

sys.path.insert(0, str(EMULATOR_DIR))
import batch_test  # noqa: E402

HALT_PC = 0x8000
STUCK_PC = 0x8001  # HALT fetch leaves PC at operand-less next byte
FRAME_PC_RE = re.compile(
    r"^Frame\s+(\d+),\s+PC=(0x[0-9A-Fa-f]+)",
    re.MULTILINE,
)


def make_im1_halt_sna(*, halt_pc: int = HALT_PC, sp: int = 0xFFFE) -> bytes:
    """Build a 48K SNA: IFF1=1, IM 1, HALT at ``halt_pc``.

    SNA 48K pops PC from the stacked word at SP. After load, PC=halt_pc and
    SP is ``sp``.

    Args:
        halt_pc: Address of the HALT opcode (must be in 0x4000–0xFFFF).
        sp: SP after the snapshot RETN pop.

    Returns:
        49179-byte .sna image.
    """
    header = bytearray(27)
    header[19] = 0x04  # bit 2 → IFF1/IFF2
    stacked_sp = (sp - 2) & 0xFFFF
    header[23] = stacked_sp & 0xFF
    header[24] = (stacked_sp >> 8) & 0xFF
    header[25] = 1  # IM 1
    ram = bytearray(49152)
    ram[stacked_sp - 0x4000] = halt_pc & 0xFF
    ram[stacked_sp - 0x4000 + 1] = (halt_pc >> 8) & 0xFF
    ram[halt_pc - 0x4000] = 0x76  # HALT
    return bytes(header) + bytes(ram)


def run_zxem(
    args: list[str],
    *,
    env_extra: dict[str, str] | None = None,
    timeout: float = 30.0,
) -> subprocess.CompletedProcess[str]:
    """Run ./zxem with cwd=emulator/ and optional extra env.

    LSAN leak-at-exit from SDL_Init is suppressed as a process status so the
    test asserts zxem's own exit code. UAF/UB still fail via sanitizer_hit().
    """
    env = os.environ.copy()
    lsan = env.get("LSAN_OPTIONS", "")
    extra_lsan = "exitcode=0"
    env["LSAN_OPTIONS"] = f"{lsan}:{extra_lsan}" if lsan else extra_lsan
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [str(ZXEM), *args],
        cwd=EMULATOR_DIR,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
        env=env,
    )


def combined_output(proc: subprocess.CompletedProcess[str]) -> str:
    """Join stdout and stderr for heartbeat + sanitizer scans."""
    return (proc.stdout or "") + (proc.stderr or "")


def test_halt_snapshot_leaves_halt_when_int_fires(tmp_path: Path) -> None:
    """EI/IFF1 + IM1 + HALT must not sit at 0x8001 for 20 frames."""
    sna_path = tmp_path / "halt_im1.sna"
    sna_path.write_bytes(make_im1_halt_sna())
    proc = run_zxem(
        [
            "--headless",
            "--frames",
            "20",
            "--no-audio",
            "--no-log",
            "--no-system-rom",
            str(sna_path),
        ],
        env_extra={"ZXEM_HEARTBEAT": "1"},
    )
    text = combined_output(proc)
    assert proc.returncode == 0, text[-2000:]
    assert not batch_test.sanitizer_hit(text), text[-2000:]
    assert "Reached target frame count 20" in proc.stdout
    assert "Loaded " in proc.stdout
    assert "PC=0x8000" in proc.stdout

    beats = list(FRAME_PC_RE.finditer(proc.stdout))
    assert len(beats) >= 3, proc.stdout
    last_pc = int(beats[-1].group(2), 16)
    last_frame = int(beats[-1].group(1))
    assert last_frame == 20, proc.stdout
    post = [int(m.group(2), 16) for m in beats if int(m.group(1)) > 0]
    assert post, proc.stdout
    assert not all(pc in (HALT_PC, STUCK_PC) for pc in post), proc.stdout
    assert last_pc not in (HALT_PC, STUCK_PC), (
        f"PC stuck in HALT (last=0x{last_pc:04X}); INT likely not delivered\n"
        f"{proc.stdout}"
    )
    live, reason = batch_test.liveness_ok(proc.stdout)
    assert live, reason


def test_liveness_rejects_stuck_halt_pc() -> None:
    frozen = (
        "Loaded halt.sna PC=0x8000 SP=0xFFFE model=spectrum48\n"
        "Frame 0, PC=0x8000 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Frame 20, PC=0x8001 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Reached target frame count 20, exiting.\n"
    )
    live, reason = batch_test.liveness_ok(frozen)
    assert not live
    assert "HALT" in reason
    analysis = batch_test.analyze(
        {"stdout": frozen, "returncode": 0, "timeout": False}
    )
    assert analysis["mode"] == "frozen"


def test_liveness_accepts_pc_move() -> None:
    moving = (
        "Loaded halt.sna PC=0x8000 SP=0xFFFE model=spectrum48\n"
        "Frame 0, PC=0x8000 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Frame 20, PC=0x1234 SP=0xFFFC FRAMES=0x000000 scr=aaaaaaaa\n"
        "Reached target frame count 20, exiting.\n"
    )
    live, reason = batch_test.liveness_ok(moving)
    assert live
    assert "PC" in reason
    analysis = batch_test.analyze(
        {"stdout": moving, "returncode": 0, "timeout": False}
    )
    assert analysis["mode"] == "ran_frames"


def test_liveness_accepts_frames_sysvar_progress() -> None:
    """HALT-sync titles may sample the same PC; FRAMES must still tick."""
    halt_sync = (
        "Loaded game.sna PC=0x9000 SP=0xFFFE model=spectrum48\n"
        "Frame 0, PC=0x9001 SP=0xFFFE FRAMES=0x000010 scr=bbbbbbbb\n"
        "Frame 50, PC=0x9001 SP=0xFFFE FRAMES=0x000042 scr=bbbbbbbb\n"
        "Reached target frame count 50, exiting.\n"
    )
    live, reason = batch_test.liveness_ok(halt_sync)
    assert live
    assert "FRAMES" in reason


def test_lsan_leak_at_exit_is_not_a_crash() -> None:
    """SDL_Init leak-at-exit must not score as sanitizer/crash (TEST-2)."""
    text = (
        "Loaded x.sna PC=0x8000 SP=0xFFFE model=spectrum48\n"
        "Frame 0, PC=0x8000 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Frame 20, PC=0x1234 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Reached target frame count 20, exiting.\n"
        "ERROR: LeakSanitizer: detected memory leaks\n"
        "SUMMARY: AddressSanitizer: 2966 byte(s) leaked in 34 allocation(s).\n"
    )
    assert not batch_test.sanitizer_hit(text)
    assert batch_test.leak_only_sanitizer(text)
    analysis = batch_test.analyze(
        {"stdout": text, "returncode": 1, "timeout": False}
    )
    assert analysis["mode"] == "ran_frames"


def test_sanitizer_is_failure_even_if_alive() -> None:
    text = (
        "Loaded x.sna PC=0x8000 SP=0xFFFE model=spectrum48\n"
        "Frame 0, PC=0x8000 SP=0xFFFE FRAMES=0x000000 scr=aaaaaaaa\n"
        "Frame 50, PC=0x1234 SP=0xFFFE FRAMES=0x000001 scr=cccccccc\n"
        "Reached target frame count 50, exiting.\n"
        "ERROR: AddressSanitizer: heap-use-after-free\n"
    )
    analysis = batch_test.analyze(
        {"stdout": text, "returncode": 0, "timeout": False}
    )
    assert analysis["mode"] == "sanitizer"
    assert batch_test.sanitizer_hit(text)
