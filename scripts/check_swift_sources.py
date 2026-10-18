#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Type-check the three iOS targets before compiling the native dependencies."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/app"
LOGS = ROOT / "build/logs"
LOGS.mkdir(parents=True, exist_ok=True)
sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
shared = sorted((APP / "Shared").rglob("*.swift"))
targets = [
    ("HuskJITHelper", "26.0", sorted((APP / "HuskJITHelper").rglob("*.swift")),
     ["-application-extension", "-F", str(APP / "Frameworks/StikJIT.xcframework/ios-arm64")]),
    ("HuskDownloadsWidget", "16.1", sorted((APP / "HuskDownloadsWidget").rglob("*.swift")) + shared,
     ["-application-extension"]),
    ("Husk", "15.5", sorted((APP / "Husk").rglob("*.swift")) + shared + [APP / "HuskJITHelper/HuskJITMessages.swift"],
     ["-import-objc-header", str(APP / "Husk/HuskBridge.h")]),
]
failed = False
for name, minimum, sources, extra in targets:
    command = ["xcrun", "swiftc", "-typecheck", "-parse-as-library", "-swift-version", "5",
               "-module-name", name, "-sdk", sdk, "-target", f"arm64-apple-ios{minimum}", *extra,
               *map(str, sources)]
    print(f"Type-checking {name} (iOS {minimum})", flush=True)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (LOGS / f"{name}-typecheck.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        failed = True
        print(result.stdout, flush=True)
    else:
        print(f"PASS: {name}", flush=True)
sys.exit(1 if failed else 0)
