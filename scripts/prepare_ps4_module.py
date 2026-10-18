#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage Husk 3.0's official PS4 module; its two source branches are not public.

Only the hash-pinned upstream module and accompanying notices are used. The
Chinese app, console host and other native dependencies are built from source.
No code from the downloaded IPA is executed by this script.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_TAG = "3.0"
UPSTREAM_COMMIT = "cffaf8b7b8eec773b4c69838e04c04b3d56b8139"
IPA_URL = "https://github.com/Leviidev/Husk/releases/download/3.0/Husk.ipa"
IPA_SHA256 = "112a1ddbb3143ae25e80c18c5b195b6dfa5067d66cbf99d2188fc03f4ab0f61d"
MODULE_PATH = "Payload/Husk.app/Frameworks/libhuskps4.dylib"
OUT = ROOT / "build/ios-arm64/lib"
PROVENANCE = "ps4-module.provenance.json"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 << 20):
            digest.update(block)
    return digest.hexdigest()


def inspect_module(data):
    """Read device architecture, dependencies and minimum OS without loading it."""
    assert len(data) >= 32, "truncated PS4 module"
    magic, cpu, _, kind, commands, command_bytes, _, _ = struct.unpack_from("<8I", data)
    assert (magic, cpu, kind) == (0xFEEDFACF, 0x0100000C, 6), "expected an arm64 Mach-O dylib"
    assert 32 + command_bytes <= len(data), "truncated Mach-O commands"
    offset, minimum, identity = 32, None, None
    dependencies = []
    for _ in range(commands):
        command, size = struct.unpack_from("<II", data, offset)
        assert size >= 8 and offset + size <= 32 + command_bytes, "invalid Mach-O command"
        if command == 0x32:  # LC_BUILD_VERSION
            platform, version = struct.unpack_from("<II", data, offset + 8)
            assert platform == 2, "PS4 module is not for iOS devices"
            minimum = (version >> 16, (version >> 8) & 255, version & 255)
        elif command == 0x25:  # LC_VERSION_MIN_IPHONEOS
            version = struct.unpack_from("<I", data, offset + 8)[0]
            minimum = (version >> 16, (version >> 8) & 255, version & 255)
        elif command in (0xC, 0xD, 0x80000018, 0x8000001F, 0x80000023, 0x20):
            start = struct.unpack_from("<I", data, offset + 8)[0]
            assert 24 <= start < size, "invalid dylib name"
            name = data[offset + start:offset + size].split(b"\0", 1)[0].decode("utf-8")
            if command == 0xD:
                identity = name
            else:
                dependencies.append(name)
        elif command in (0x21, 0x2C):  # LC_ENCRYPTION_INFO / _64
            assert struct.unpack_from("<I", data, offset + 16)[0] == 0, "encrypted module"
        offset += size
    assert offset == 32 + command_bytes, "Mach-O command size mismatch"
    assert identity == "@rpath/libhuskps4.dylib", identity
    assert minimum is not None and minimum <= (17, 4, 0), minimum
    for dependency in dependencies:
        assert dependency.startswith(("/usr/lib/", "/System/Library/Frameworks/")) or dependency in (
            "@rpath/MoltenVK.framework/MoltenVK", "@rpath/libMoltenVK.dylib",
        ), f"unaccounted PS4 dependency: {dependency}"
    return {"architecture": "arm64", "minimum_ios": ".".join(map(str, minimum)),
            "install_name": identity, "dependencies": dependencies}


def download():
    cache = ROOT / "build/upstream-ps4-3.0"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "Husk.ipa"
    if archive.is_file() and sha256(archive) == IPA_SHA256:
        return archive
    partial = cache / "Husk.ipa.part"
    subprocess.run(["curl", "--fail", "--location", "--retry", "5", "--retry-delay", "5",
                    "--connect-timeout", "30", "--speed-limit", "1024", "--speed-time", "60",
                    "--max-time", "1200", "--output", str(partial), IPA_URL], check=True)
    assert sha256(partial) == IPA_SHA256, "upstream IPA SHA-256 mismatch; refusing to use it"
    partial.replace(archive)
    return archive


def prepare(archive):
    assert sha256(archive) == IPA_SHA256, "upstream IPA SHA-256 mismatch; refusing to extract it"
    OUT.mkdir(parents=True, exist_ok=True)
    notices = []
    with zipfile.ZipFile(archive) as z:
        data = z.read(MODULE_PATH)
        metadata = inspect_module(data)
        module = OUT / "libhuskps4.dylib"
        module.write_bytes(data)
        module.chmod(0o755)
        # Retain all licence/notice files shipped with the official app. Paths
        # remain relative to this dedicated directory, never to the checkout.
        for entry in z.infolist():
            path = PurePosixPath(entry.filename)
            if entry.is_dir() or not path.name.lower().startswith(("license", "licence", "notice", "copying")):
                continue
            assert not path.is_absolute() and ".." not in path.parts and "\\" not in entry.filename
            assert not stat.S_ISLNK(entry.external_attr >> 16) and entry.file_size < 32 << 20
            content = z.read(entry)
            dest = OUT / "ps4-upstream-notices" / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(content)
            notices.append({"archive_path": entry.filename, "sha256": hashlib.sha256(content).hexdigest()})
    report = {"component": "libhuskps4.dylib", "source": "official upstream prebuilt module",
              "upstream_tag": UPSTREAM_TAG, "upstream_commit": UPSTREAM_COMMIT,
              "source_ipa_url": IPA_URL, "source_ipa_sha256": IPA_SHA256,
              "archive_path": MODULE_PATH, "upstream_module_sha256": sha256(module),
              "upstream_module_bytes": len(data), "mach_o": metadata, "notices": notices,
              "packaging": "The unchanged upstream module is staged, then ad-hoc signed with the Chinese app."}
    (OUT / PROVENANCE).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if sys.platform == "darwin":
        symbols = subprocess.check_output(["nm", "-gU", str(module)], text=True)
        assert {line.split()[-1] for line in symbols.splitlines() if line.strip()} == {"_husk_console_module_v1"}, symbols


def stage_bundle(bundle):
    report = json.loads((OUT / PROVENANCE).read_text(encoding="utf-8"))
    assert report["source_ipa_sha256"] == IPA_SHA256
    assert sha256(bundle / "Frameworks/libhuskps4.dylib") == report["upstream_module_sha256"], "embedded PS4 module differs before signing"
    shutil.copyfile(OUT / PROVENANCE, bundle / PROVENANCE)
    for notice in report["notices"]:
        rel = Path("ps4-upstream-notices") / notice["archive_path"]
        assert sha256(OUT / rel) == notice["sha256"]
        destination = bundle / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(OUT / rel, destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--ipa", type=Path, help="Use an already downloaded, hash-verified official IPA")
    group.add_argument("--stage-bundle", type=Path, help="Verify the embedded module and copy its provenance before signing")
    args = parser.parse_args()
    if args.stage_bundle:
        stage_bundle(args.stage_bundle)
    else:
        prepare(args.ipa or download())
