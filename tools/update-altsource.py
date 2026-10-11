#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Regenerate altsource.json, the AltStore/SideStore source, from the GitHub releases.

    python3 tools/update-altsource.py

Run it after publishing a release and commit the result. Sideloaders read the file from raw.githubusercontent.com, so a pushed
change is what they see. Each release's Husk.ipa becomes a version, newest first, with its real size and date.
"""
import json
import os
import re
import urllib.request

REPO = "Leviidev/Husk"
RAW = f"https://raw.githubusercontent.com/{REPO}/main"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "altsource.json")
KEEP = 10   # how many releases to list


def project_min_os():
    """The deployment target in project.yml: what the newest release needs."""
    with open(os.path.join(ROOT, "src/app/project.yml")) as f:
        m = re.search(r'deploymentTarget:\s*\n(?:\s*#.*\n)*\s*iOS:\s*"([\d.]+)"', f.read())
    return m.group(1) if m else "16.0"


def min_os(version, newest_min):
    # Releases before 1.0.0 were built for iOS 16.4; from 1.0.0 on, the floor is the project's.
    parts = [int(p) for p in re.findall(r"\d+", version)[:3]] + [0, 0, 0]
    return "16.4" if parts[:3] < [1, 0, 0] else newest_min


def releases():
    req = urllib.request.Request(f"https://api.github.com/repos/{REPO}/releases?per_page=30",
                                 headers={"User-Agent": "husk-altsource", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def main():
    newest_min = project_min_os()
    versions = []
    for rel in releases():
        if rel.get("draft") or rel.get("prerelease"):
            continue
        ipa = next((a for a in rel["assets"] if a["name"].lower().endswith(".ipa")), None)
        if not ipa:
            continue
        version = rel["tag_name"].lstrip("vV")
        notes = (rel.get("body") or "").strip() or f"Husk {version}."
        versions.append({
            "version": version,
            "date": rel["published_at"],
            "localizedDescription": notes[:4000],
            "downloadURL": ipa["browser_download_url"],
            "size": ipa["size"],
            "minOSVersion": min_os(version, newest_min),
        })
        if len(versions) >= KEEP:
            break

    icon = f"{RAW}/src/app/Husk/Assets.xcassets/AppIcon.appiconset/Icon-Default.png"
    shots = sorted(f for f in os.listdir(os.path.join(ROOT, "Screenshots")) if f.lower().endswith((".png", ".jpeg", ".jpg")))
    source = {
        "name": "Husk",
        "identifier": "com.leviidev.husk.source",
        "subtitle": "Android apps and games on your iPhone.",
        "description": "The official source for Husk. Add it to SideStore, AltStore or another sideloader to install Husk and get its updates.",
        "iconURL": icon,
        "website": f"https://github.com/{REPO}",
        "tintColor": "#5865F2",
        "featuredApps": ["com.husk.app"],
        "apps": [{
            "name": "Husk",
            "bundleIdentifier": "com.husk.app",
            "developerName": "Leviidev",
            "subtitle": "Android apps and games on your iPhone.",
            "localizedDescription": ("Husk runs Android apps and games on iPhone. Its Translation Layer runs games made with Unity, "
                                     "Unreal, Godot, cocos2d-x, SDL and more natively, and a full Android system is there for "
                                     "everything else. Husk needs JIT: StikDebug, its built-in StikJIT, TrollStore or a jailbreak."),
            "iconURL": icon,
            "tintColor": "#5865F2",
            "category": "games",
            "screenshots": [f"{RAW}/Screenshots/{s}" for s in shots],
            "versions": versions,
            "appPermissions": {
                "entitlements": [
                    "get-task-allow",
                    "dynamic-codesigning",
                    "com.apple.developer.kernel.increased-memory-limit",
                    "com.apple.developer.kernel.extended-virtual-addressing",
                ],
                "privacy": {
                    "NSLocalNetworkUsageDescription": "Husk uses the local network to pair with this iPhone for JIT, without a computer.",
                },
            },
        }],
        "news": [],
    }
    with open(OUT, "w") as f:
        json.dump(source, f, indent=2)
        f.write("\n")
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(versions)} versions, newest {versions[0]['version'] if versions else 'none'}")


if __name__ == "__main__":
    main()
