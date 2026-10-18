#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Repair cached QEMU Python entry points without discarding native objects."""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "third_party/build/qemu-10.0.12-utm/_husk_build"


def main():
    environment = BUILD / "pyvenv"
    python = environment / "bin/python3"
    info = json.loads((BUILD / "meson-info/meson-info.json").read_text(encoding="utf-8"))
    version = info["meson_version"]["full"]
    assert re.fullmatch(r"\d+\.\d+\.\d+", version), version
    # Homebrew may have replaced the interpreter since the cache was saved.
    try:
        usable = subprocess.run([str(python), "--version"], capture_output=True).returncode == 0
    except OSError:
        usable = False
    if not usable:
        print("Refreshing QEMU's cached Python interpreter", flush=True)
        for link in (environment / "bin").glob("python*"):
            if link.is_symlink():
                link.unlink()
        subprocess.run([sys.executable, "-m", "venv", "--upgrade", str(environment)], check=True)
    current = subprocess.run([str(python), "-m", "mesonbuild.mesonmain", "--version"], capture_output=True, text=True)
    if current.returncode or current.stdout.strip() != version:
        subprocess.run([str(python), "-m", "ensurepip", "--upgrade"], check=True)
        subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", f"meson=={version}"], check=True)
    # A short shell shebang avoids the interpreter-path limit of macOS execve.
    # Use the same Meson version as coredata.dat to preserve incremental builds.
    launcher = environment / "bin/meson"
    launcher.write_text('#!/bin/sh\nexec "$(dirname "$0")/python3" -m mesonbuild.mesonmain "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    actual = subprocess.check_output([str(launcher), "--version"], text=True).strip()
    assert actual == version, (actual, version)
    print(f"QEMU Meson launcher ready: {actual}", flush=True)


if __name__ == "__main__":
    main()
