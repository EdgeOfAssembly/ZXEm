#!/usr/bin/env python3
"""Headless batch test runner for the ZXEm ZX Spectrum emulator.

Runs ./zxem on each supported file under ZXEM_GAMES (default ./games) with --headless --frames N,
captures stdout/stderr/exit status, and writes a JSON test log.

Pass is not "process still alive". ZXEM_HEARTBEAT=1 makes zxem print
``Frame N, PC=... FRAMES=... scr=...`` at load, every 50 frames, and at exit.
A run is live if PC moved (not just HALT fetch +1) or FRAMES/scr progressed.
Sanitizer diagnostics fail the run even if the process exits 0.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

EMULATOR_DIR = Path(__file__).resolve().parent
ZXEM = EMULATOR_DIR / "zxem"
GAMES_DIR = Path(os.environ.get("ZXEM_GAMES", str(EMULATOR_DIR / "games")))
LOG_FILE = EMULATOR_DIR / "batch_test_log.json"
MAX_FILES = 200
FRAMES = 50
TIMEOUT_SECONDS = 10

SUPPORTED_EXTENSIONS = {
    ".z80",
    ".sna",
    ".tap",
    ".tzx",
    ".szx",
    ".sp",
    ".slt",
    ".scl",
    ".trd",
    ".rom",
    ".dck",
}

PASS_MODES = ("ran_ok", "ran_frames")

# Real defects only. LeakSanitizer-at-exit from SDL_Init is pre-existing (AUD-1)
# and would mark every ASan headless run as a crash.
SANITIZER_MARKERS = (
    "ERROR: AddressSanitizer:",
    "ERROR: UndefinedBehaviorSanitizer",
    "ERROR: ThreadSanitizer",
    "runtime error:",
)

FRAME_LINE_RE = re.compile(
    r"^Frame\s+(\d+),\s+PC=(0x[0-9A-Fa-f]+)\s+SP=(0x[0-9A-Fa-f]+)"
    r"(?:\s+FRAMES=(0x[0-9A-Fa-f]+))?(?:\s+scr=([0-9A-Fa-f]+))?",
    re.MULTILINE,
)
LOADED_PC_RE = re.compile(r"Loaded .* PC=(0x[0-9A-Fa-f]+)")
READY_PC_RE = re.compile(r"ready PC=(0x[0-9A-Fa-f]+)")


def sanitizer_hit(text: str) -> bool:
    """True if ASan/UBSan reported a memory/UB defect (not leak-at-exit)."""
    return any(marker in text for marker in SANITIZER_MARKERS)


def leak_only_sanitizer(text: str) -> bool:
    """True if LSAN reported leaks and there is no UAF/UB diagnostic."""
    if sanitizer_hit(text):
        return False
    return "ERROR: LeakSanitizer" in text or "byte(s) leaked" in text


def parse_heartbeats(stdout: str) -> list[dict[str, Any]]:
    """Parse ``Frame N, PC=...`` heartbeat lines from zxem stdout.

    Args:
        stdout: Combined emulator output.

    Returns:
        One dict per heartbeat with frame, pc, sp, and optional frames/scr.
    """
    samples: list[dict[str, Any]] = []
    for m in FRAME_LINE_RE.finditer(stdout):
        rec: dict[str, Any] = {
            "frame": int(m.group(1)),
            "pc": int(m.group(2), 16),
            "sp": int(m.group(3), 16),
        }
        if m.group(4) is not None:
            rec["frames"] = int(m.group(4), 16)
        if m.group(5) is not None:
            rec["scr"] = m.group(5).lower()
        samples.append(rec)
    return samples


def liveness_ok(stdout: str) -> tuple[bool, str]:
    """Decide whether headless output shows the machine still doing work.

    A HALT-frozen CPU (dead INT) typically yields two PCs one apart
    (opcode fetch then stuck) with unchanged FRAMES and display hash.

    Args:
        stdout: Combined emulator output including heartbeat lines.

    Returns:
        ``(True, reason)`` if PC/FRAMES/scr show progress, else ``(False, reason)``.
    """
    samples = parse_heartbeats(stdout)
    pcs: list[int] = [s["pc"] for s in samples]
    loaded = LOADED_PC_RE.search(stdout)
    ready = READY_PC_RE.search(stdout)
    if loaded:
        pcs.insert(0, int(loaded.group(1), 16))
    elif ready:
        pcs.insert(0, int(ready.group(1), 16))

    frames_vals = [s["frames"] for s in samples if "frames" in s]
    scrs = [s["scr"] for s in samples if "scr" in s]

    if len(set(scrs)) >= 2:
        return True, "display hash changed"
    if len(frames_vals) >= 2 and frames_vals[-1] != frames_vals[0]:
        return True, "FRAMES sysvar advanced"

    unique = set(pcs)
    if len(unique) >= 3:
        return True, "PC histogram moved"
    if len(unique) == 2:
        a, b = sorted(unique)
        if b == a + 1:
            return False, f"PC stuck after HALT fetch (0x{a:04X}→0x{b:04X})"
        return True, "PC changed"
    if len(unique) == 1 and pcs:
        return False, f"PC frozen at 0x{pcs[-1]:04X}"
    return False, "no PC/FRAMES/scr heartbeat"


def gather_files(limit: int = MAX_FILES) -> list[Path]:
    files: list[Path] = []
    for root, _dirs, fnames in os.walk(GAMES_DIR):
        for name in sorted(fnames):
            ext = os.path.splitext(name)[1].lower()
            if ext in SUPPORTED_EXTENSIONS:
                files.append(Path(root) / name)
                if len(files) >= limit:
                    return files
    return files


def classify_file(path: Path) -> dict[str, Any]:
    ext = path.suffix.lower()
    info: dict[str, Any] = {"format": ext[1:]}
    try:
        with open(path, "rb") as f:
            header = f.read(30)
        if len(header) < 30:
            info["reason"] = "file_too_short"
            return info
        if ext == ".z80":
            pc = header[6] | (header[7] << 8)
            info["is_v1"] = pc != 0
            info["pc"] = f"0x{pc:04X}"
            info["compressed"] = (header[12] & 0x20) != 0
        elif ext == ".sna":
            info["sp"] = f"0x{header[23] | (header[24] << 8):04X}"
            info["border"] = header[26] & 0x07
    except OSError as e:
        info["reason"] = "read_error"
        info["error"] = str(e)
    return info


def run_test(path: Path) -> dict[str, Any]:
    start = time.monotonic()
    env = os.environ.copy()
    env["ZXEM_HEARTBEAT"] = "1"
    proc = subprocess.Popen(
        [str(ZXEM), "--headless", "--frames", str(FRAMES), "--no-audio", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=EMULATOR_DIR,
        env=env,
    )
    try:
        stdout_bytes, _ = proc.communicate(timeout=TIMEOUT_SECONDS)
        stdout = stdout_bytes.decode("utf-8", errors="replace")
        elapsed = time.monotonic() - start
        killed_by_timeout = False
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout_bytes, _ = proc.communicate(timeout=2)
        stdout = stdout_bytes.decode("utf-8", errors="replace")
        elapsed = time.monotonic() - start
        killed_by_timeout = True

    return {
        "returncode": proc.returncode,
        "stdout": stdout,
        "duration_seconds": round(elapsed, 3),
        "timeout": killed_by_timeout,
    }


def analyze(result: dict[str, Any]) -> dict[str, Any]:
    stdout = result["stdout"]
    rc = result["returncode"]
    lines = stdout.splitlines()

    loaded_line = next((ln for ln in lines if ln.startswith("Loaded ")), None)
    failure_line = next((ln for ln in lines if "Failed to load" in ln), None)
    frame_lines = [ln for ln in lines if ln.startswith("Frame ")]
    last_frame_line = frame_lines[-1] if frame_lines else None
    reached_line = next(
        (ln for ln in lines if ln.startswith("Reached target frame count")), None
    )

    model_used = "unknown"
    if loaded_line:
        if "model=spectrum128" in loaded_line:
            model_used = "spectrum128"
        elif "model=spectrum48" in loaded_line:
            model_used = "spectrum48"

    live, live_reason = liveness_ok(stdout)

    if sanitizer_hit(stdout):
        mode = "sanitizer"
    elif "v2/v3 Z80 snapshots are not supported" in stdout:
        mode = "unsupported_v2v3"
    elif "Failed to load" in stdout:
        mode = "load_failure"
    elif result["timeout"] and frame_lines:
        mode = "ran_ok" if live else "frozen"
    elif result["timeout"] and not frame_lines:
        mode = "hung"
    elif rc != 0 and leak_only_sanitizer(stdout) and (reached_line or frame_lines):
        mode = "ran_frames" if live else "frozen"
    elif rc != 0:
        mode = "crash"
    elif reached_line or frame_lines:
        mode = "ran_frames" if live else "frozen"
    elif loaded_line:
        mode = "loaded_no_frames"
    else:
        mode = "unknown"

    return {
        "loaded_line": loaded_line,
        "failure_line": failure_line,
        "last_frame_line": last_frame_line,
        "frame_count": len(frame_lines),
        "mode": mode,
        "model_used": model_used,
        "live": live,
        "live_reason": live_reason,
    }


def main() -> None:
    if not ZXEM.is_file():
        print(f"Emulator binary not found: {ZXEM}", file=sys.stderr)
        sys.exit(1)

    files = gather_files(MAX_FILES)
    print(f"Testing {len(files)} files (max {MAX_FILES})...")

    results: list[dict[str, Any]] = []
    for i, path in enumerate(files, start=1):
        info = classify_file(path)
        fmt = info.get("format", "unknown")
        print(f"[{i}/{len(files)}] {path.name} ({fmt}) ...", end="", flush=True)
        run = run_test(path)
        analysis = analyze(run)
        record = {
            "index": i,
            "path": str(path),
            "format": fmt,
            "file_info": info,
            "returncode": run["returncode"],
            "duration_seconds": run["duration_seconds"],
            "timeout": run["timeout"],
            "mode": analysis["mode"],
            "model_used": analysis["model_used"],
            "loaded_line": analysis["loaded_line"],
            "failure_line": analysis["failure_line"],
            "last_frame_line": analysis["last_frame_line"],
            "frame_count": analysis["frame_count"],
            "live": analysis["live"],
            "live_reason": analysis["live_reason"],
            "stdout": run["stdout"],
        }
        results.append(record)
        print(
            f" {record['mode']} rc={record['returncode']} "
            f"time={record['duration_seconds']}s frames={record['frame_count']}"
            f" live={record['live']}"
        )

    with open(LOG_FILE, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)

    total = len(results)
    modes: dict[str, int] = {}
    formats: dict[str, int] = {}
    for r in results:
        modes[r["mode"]] = modes.get(r["mode"], 0) + 1
        fmt = r.get("format", "unknown")
        formats[fmt] = formats.get(fmt, 0) + 1

    print("\n=== Summary ===")
    print(f"Total tested: {total}")
    print("\nBy format:")
    for fmt, count in sorted(formats.items(), key=lambda x: -x[1]):
        print(f"  {fmt}: {count}")
    print("\nBy mode:")
    for mode, count in sorted(modes.items(), key=lambda x: -x[1]):
        print(f"  {mode}: {count}")

    failures = [r for r in results if r["mode"] not in PASS_MODES]
    print(f"\nFailed/unsupported/hung/frozen: {len(failures)}")
    for r in failures[:10]:
        print(f"  {r['index']}. {Path(r['path']).name}: {r['mode']} rc={r['returncode']}")
        if r["failure_line"]:
            print(f"      -> {r['failure_line']}")
        if r.get("live_reason"):
            print(f"      -> {r['live_reason']}")

    print(f"\nFull log written to: {LOG_FILE}")


if __name__ == "__main__":
    main()
