#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the Chinese source catalog, native format arguments and bundle locales."""
import json
from pathlib import Path
import plistlib
import re

ROOT = Path(__file__).resolve().parents[1]
PRINTF = re.compile(r"%(?:\d+\$)?[-+#0 ']*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]")


def main():
    rows = []
    for name in ("zh-Hans.json", "native-zh-Hans.json"):
        catalog = json.loads((ROOT / "localization" / name).read_text(encoding="utf-8"))
        for entry in catalog:
            source = (ROOT / entry["file"]).read_text(encoding="utf-8")
            assert entry["zh-Hans"] in source, (
                entry["file"], entry["line"], entry["zh-Hans"]
            )
            if name.startswith("native-"):
                assert PRINTF.findall(entry["en"]) == PRINTF.findall(entry["zh-Hans"]), entry
                if entry["file"].endswith(".rs"):
                    assert re.findall(r"\{[^{}]+\}", entry["en"]) == re.findall(r"\{[^{}]+\}", entry["zh-Hans"]), entry
        rows.extend(catalog)
    versions = []
    for target in ("Husk", "HuskJITHelper", "HuskDownloadsWidget"):
        with open(ROOT / "src/app" / target / "Info.plist", "rb") as f:
            info = plistlib.load(f)
        assert info["CFBundleDevelopmentRegion"] == "zh-Hans", target
        assert "zh-Hans" in info["CFBundleLocalizations"], target
        assert info["CFBundleShortVersionString"] == "3.0.0", target
        versions.append(info["CFBundleVersion"])
    assert len(set(versions)) == 1, ("app/extension build mismatch", versions)
    assert versions[0] == "129", versions
    for path in (ROOT / "src/app").rglob("*.swift"):
        assert not re.search(r"^(<<<<<<<|=======|>>>>>>>)", path.read_text(encoding="utf-8"), re.M), path
    print(f"PASS: {len(rows)} catalog entries in {len({r['file'] for r in rows})} files; three Chinese bundle plists.")


if __name__ == "__main__":
    main()
