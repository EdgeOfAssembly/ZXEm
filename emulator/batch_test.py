#!/usr/bin/env python3
"""Headless batch test runner for the ZXEm ZX Spectrum emulator.

Runs ./zxem on each supported file under /mnt/games with --headless --frames N,
captures stdout/stderr/exit status, and writes a JSON test log.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

EMULATOR_DIR = Path(__file__).resolve().parent
ZXEM = EMULATOR_DIR / "zxem"
GAMES_DIR = Path("/mnt/games")
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


def gather_files(limit=MAX_FILES):
    files = []
    for root, _dirs, fnames in os.walk(GAMES_DIR):
        for name in sorted(fnames):
            ext = os.path.splitext(name)[1].lower()
            if ext in SUPPORTED_EXTENSIONS:
                files.append(Path(root) / name)
                if len(files) >= limit:
                    return files
    return files


def classify_file(path: Path):
    ext = path.suffix.lower()
    info = {"format": ext[1:]}
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
    except Exception as e:
        info["reason"] = "read_error"
        info["error"] = str(e)
    return info


def run_test(path: Path):
    start = time.monotonic()
    proc = subprocess.Popen(
        [str(ZXEM), "--headless", "--frames", str(FRAMES), str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=EMULATOR_DIR,
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


def analyze(result):
    stdout = result["stdout"]
    rc = result["returncode"]
    lines = stdout.splitlines()

    loaded_line = next(
        (ln for ln in lines if ln.startswith("Loaded ")), None
    )
    failure_line = next(
        (ln for ln in lines if "Failed to load" in ln), None
    )
    error_line = next(
        (ln for ln in lines if "Error:" in ln or "error" in ln.lower()), None
    )
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

    if "v2/v3 Z80 snapshots are not supported" in stdout:
        mode = "unsupported_v2v3"
    elif "Failed to load" in stdout:
        mode = "load_failure"
    elif result["timeout"] and frame_lines:
        mode = "ran_ok"
    elif result["timeout"] and not frame_lines:
        mode = "hung"
    elif rc != 0:
        mode = "crash"
    elif reached_line:
        mode = "ran_frames"
    elif frame_lines:
        mode = "ran_frames"
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
    }


def main():
    if not ZXEM.is_file():
        print(f"Emulator binary not found: {ZXEM}", file=sys.stderr)
        sys.exit(1)

    files = gather_files(MAX_FILES)
    print(f"Testing {len(files)} files (max {MAX_FILES})...")

    results = []
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
            "stdout": run["stdout"],
        }
        results.append(record)
        print(f" {record['mode']} rc={record['returncode']} "
              f"time={record['duration_seconds']}s frames={record['frame_count']}")

    with open(LOG_FILE, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)

    total = len(results)
    modes = {}
    formats = {}
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

    failures = [r for r in results if r["mode"] not in ("ran_ok", "ran_frames", "loaded_no_frames")]
    print(f"\nFailed/unsupported/hung: {len(failures)}")
    for r in failures[:10]:
        print(f"  {r['index']}. {Path(r['path']).name}: {r['mode']} rc={r['returncode']}")
        if r["failure_line"]:
            print(f"      -> {r['failure_line']}")

    print(f"\nFull log written to: {LOG_FILE}")


if __name__ == "__main__":
    main()
