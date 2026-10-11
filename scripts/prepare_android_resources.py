#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare the Android resources needed by the 2.0 Java/Flutter runtime on CI.

The upstream convenience script uses paths from the author's Mac. This uses
the public Lineage image instead, verifies its release hash, extracts the four
APEX modules without mounting or executing them, and calls the upstream DEX
builder. qemu-img, debugfs, a JDK and Python 3 are required.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BASE = "https://github.com/Leviidev/Husk/releases/download/lineage-v2/"
R8_VERSION = "8.5.35"


def run(*args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def download(url, dest):
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_file() and dest.stat().st_size > 0:
        return
    part = dest.with_suffix(dest.suffix + ".part")
    run("curl", "-fL", "--retry", "4", "--retry-delay", "5", "-o", part, url)
    part.replace(dest)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        while block := f.read(8 << 20):
            digest.update(block)
    return digest.hexdigest()


def unpack_system(raw, system):
    """Read GPT and Android logical-partition metadata, without a privileged mount."""
    with open(raw, "rb") as f:
        f.seek(512)
        header = f.read(512)
        assert header[:8] == b"EFI PART", "not a GPT disk"
        entries_lba = struct.unpack_from("<Q", header, 72)[0]
        count, stride = struct.unpack_from("<II", header, 80)
        assert 2 <= count <= 256 and 128 <= stride <= 4096
        f.seek(entries_lba * 512 + stride)
        second = f.read(stride)
        base = struct.unpack_from("<Q", second, 32)[0] * 512
        f.seek(base + 12288)
        h = f.read(256)
        magic, _, _, hsize = struct.unpack_from("<IHHI", h, 0)
        assert magic == 0x414C5030, "no logical partition metadata in partition 2"
        tsize = struct.unpack_from("<I", h, 44)[0]
        assert hsize <= 4096 and tsize <= 64 << 20
        descs = [struct.unpack_from("<III", h, 80 + 12 * i) for i in range(4)]
        f.seek(base + 12288 + hsize)
        table = f.read(tsize)
        (po, pn, ps), (eo, en, es) = descs[:2]
        extents = [struct.unpack_from("<QIQI", table, eo + es * i) for i in range(en)]
        for i in range(pn):
            entry = table[po + ps * i:po + ps * (i + 1)]
            name = entry[:36].split(b"\0")[0].decode()
            _, first, number, _ = struct.unpack_from("<IIII", entry, 36)
            if name not in ("system", "system_a"):
                continue
            with open(system, "wb") as out:
                for nsec, kind, sector, source in extents[first:first + number]:
                    assert kind == 0 and source == 0, "unsupported logical extent"
                    f.seek(base + sector * 512)
                    left = nsec * 512
                    while left:
                        block = f.read(min(left, 8 << 20))
                        assert block, "truncated system extent"
                        out.write(block)
                        left -= len(block)
            return
    raise RuntimeError("system partition was not found")


def main():
    BUILD.mkdir(exist_ok=True)
    manifest_path = BUILD / "android-resource-manifest.json"
    download(BASE + "manifest.json", manifest_path)
    manifest = json.loads(manifest_path.read_text())
    image = manifest["image"]
    assert Path(image["file"]).name == image["file"], "invalid image filename"
    target = BUILD / "android-system"
    apex = BUILD / "android-apex"
    extracted = BUILD / "android-resources"
    stamp = extracted / "complete.sha256"
    debugfs = shutil.which("debugfs")
    if not debugfs:
        prefix = subprocess.check_output(["brew", "--prefix", "e2fsprogs"], text=True).strip()
        debugfs = str(Path(prefix) / "sbin/debugfs")

    def dump(system, remote, local):
        local.parent.mkdir(parents=True, exist_ok=True)
        run(debugfs, "-R", f"dump {remote} {local}", system)
        return local.is_file() and local.stat().st_size > 0

    if not stamp.exists() or stamp.read_text().strip() != image["sha256"]:
        disk = BUILD / "downloads" / image["file"]
        download(BASE + image["file"], disk)
        assert disk.stat().st_size == image["size"], "image size mismatch"
        assert sha256(disk) == image["sha256"], "image SHA-256 mismatch"
        with tempfile.TemporaryDirectory(prefix="husk-android-") as temporary:
            work = Path(temporary)
            raw, system = work / "vda.raw", work / "system.img"
            run("qemu-img", "convert", "-O", "raw", disk, raw)
            unpack_system(raw, system)
            target.mkdir(parents=True, exist_ok=True)
            run(debugfs, "-R", f"rdump /system/fonts {target}", system)
            for name in ("fonts.xml", "font_fallback.xml"):
                assert dump(system, "/system/etc/" + name, target / "etc" / name)
            for module in ("com.android.art", "com.android.i18n", "com.android.tzdata", "com.android.conscrypt"):
                package = work / (module + ".apex")
                for suffix in (".capex", ".apex"):
                    if dump(system, "/system/apex/" + module + suffix, package):
                        break
                else:
                    raise RuntimeError("missing APEX: " + module)
                with zipfile.ZipFile(package) as z:
                    if "original_apex" in z.namelist():
                        data = z.read("original_apex")
                    else:
                        data = package.read_bytes()
                with zipfile.ZipFile(io.BytesIO(data)) as z:
                    payload = work / (module + ".img")
                    payload.write_bytes(z.read("apex_payload.img"))
                destination = apex / module
                destination.mkdir(parents=True, exist_ok=True)
                run(debugfs, "-R", f"rdump / {destination}", payload)
            certs = apex / "com.android.conscrypt/cacerts"
            assert certs.is_dir() and any(certs.iterdir()), "CA certificates missing"
            shutil.copytree(certs, target / "cacerts", dirs_exist_ok=True)
            assert dump(system, "/system/framework/org.apache.http.legacy.jar", extracted / "org.apache.http.legacy.jar")
            assert dump(system, "/system/framework/framework-res.apk", extracted / "framework-res.apk")
            assert (target / "fonts/Roboto-Regular.ttf").exists(), "system fonts missing"
            # Retain CJK fonts: this Chinese build must be able to draw Chinese text.
            assert any(target.glob("fonts/*CJK*")), "CJK fonts missing"
            stamp.write_text(image["sha256"] + "\n")
    r8 = BUILD / "tools" / f"r8-{R8_VERSION}.jar"
    download(f"https://dl.google.com/dl/android/maven2/com/android/tools/r8/{R8_VERSION}/r8-{R8_VERSION}.jar", r8)
    env = os.environ.copy()
    env.update(
        HUSK_JDK=os.environ["JAVA_HOME"],
        HUSK_D8=str(r8),
        HTTP_LEGACY=str(extracted / "org.apache.http.legacy.jar"),
        FRAMEWORK_RES=str(extracted / "framework-res.apk"),
    )
    run("bash", ROOT / "scripts/build_java_runtime.sh", apex, env=env)
    runtime = BUILD / "java-runtime"
    for module in apex.iterdir():
        for notice in module.rglob("NOTICE*"):
            if notice.is_file():
                dest = runtime / module.name / notice.relative_to(module)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, dest)
    assert (runtime / "husk-framework.dex").stat().st_size > 1000
    assert (runtime / "framework-res.apk").stat().st_size > 1000
    print("Android resources ready; manifest image:", image["file"], image["sha256"])


if __name__ == "__main__":
    main()
