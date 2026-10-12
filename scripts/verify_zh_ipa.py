#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check archive integrity, Chinese metadata/text and required runtime resources."""
import hashlib
import json
from pathlib import Path
import plistlib
import sys
import zipfile


def verify(path):
    root = "Payload/Husk.app/"
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None, "archive CRC check failed"
        names = set(z.namelist())

        def required(name):
            full = root + name
            assert full in names and z.getinfo(full).file_size > 0, f"missing resource: {name}"

        def plist(name):
            return plistlib.loads(z.read(root + name))

        info = plist("Info.plist")
        assert info["CFBundleShortVersionString"] == "2.0.0", info
        assert info["CFBundleDevelopmentRegion"] == "zh-Hans", info
        assert "zh-Hans" in info["CFBundleLocalizations"], info
        assert "本地网络" in info["NSLocalNetworkUsageDescription"], info
        required(info["CFBundleExecutable"])
        executable = z.read(root + info["CFBundleExecutable"])
        for label in ("添加游戏或应用", "常见问题", "兼容性", "分享报告", "简体中文界面"):
            assert label.encode("utf-8") in executable, f"Chinese UI text missing: {label}"
        for name in (
            "Frameworks/libqemu-aarch64-softmmu.dylib",
            "Frameworks/libANGLE-shared.dylib",
            "Frameworks/libFilePickerFix.dylib",
            "Frameworks/StikJIT.framework/StikJIT",
            "Frameworks/MoltenVK.framework/MoltenVK",
            "vmlinuz-virt", "initramfs-virt", "husk-jit.js",
            "edk2-aarch64-code.fd", "lineage-efi-vars-seed.fd", "lineage-vdb-seed.qcow2",
            "java-runtime/husk-framework.dex", "java-runtime/framework-res.apk",
            "java-runtime/org.apache.http.legacy.jar",
            "java-runtime/com.android.art/javalib/core-oj.jar",
            "java-runtime/com.android.art/javalib/core-libart.jar",
            "java-runtime/com.android.art/javalib/okhttp.jar",
            "java-runtime/com.android.art/javalib/bouncycastle.jar",
            "java-runtime/com.android.art/javalib/apache-xml.jar",
            "java-runtime/com.android.i18n/javalib/core-icu4j.jar",
            "java-runtime/com.android.conscrypt/javalib/conscrypt.jar",
            "android-system/etc/fonts.xml", "android-system/etc/font_fallback.xml",
        ):
            required(name)
        cjk = [n for n in names if n.startswith(root + "android-system/fonts/") and "CJK" in n and z.getinfo(n).file_size > 0]
        certs = [n for n in names if n.startswith(root + "android-system/cacerts/") and z.getinfo(n).file_size > 0]
        assert cjk, "CJK fonts missing"
        assert certs, "CA certificates missing"
        for target in ("HuskJITHelper", "HuskDownloadsWidget"):
            base = f"PlugIns/{target}.appex/"
            required(base + target)
            extension = plist(base + "Info.plist")
            assert extension["CFBundleShortVersionString"] == info["CFBundleShortVersionString"], target
            assert extension["CFBundleVersion"] == info["CFBundleVersion"], target
            assert extension["CFBundleDevelopmentRegion"] == "zh-Hans", target
            assert "zh-Hans" in extension["CFBundleLocalizations"], target
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        while block := f.read(8 << 20):
            digest.update(block)
    report = {
        "file": path.name,
        "version": info["CFBundleShortVersionString"],
        "build": info["CFBundleVersion"],
        "commit": info.get("HuskBuildCommit"),
        "minimum_ios": info["MinimumOSVersion"],
        "localizations": info["CFBundleLocalizations"],
        "bytes": path.stat().st_size,
        "sha256": digest.hexdigest(),
        "archive_crc": "passed",
        "chinese_metadata_and_ui_string": "passed",
        "required_resources": "passed",
        "cjk_font_files": len(cjk),
        "ca_certificates": len(certs),
        "device_runtime_test": "not performed",
    }
    destination = path.with_suffix(path.suffix + ".verification.json")
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        verify(Path(argument))
    if len(sys.argv) < 2:
        raise SystemExit("usage: verify_zh_ipa.py <file.ipa> [file.tipa]")
