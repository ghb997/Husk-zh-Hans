#!/usr/bin/env python3
"""Write showcase/index.json from the pictures in showcase/.

Husk downloads this index on every launch and fetches the pictures for the apps a person has. A picture is named
<android package>_screenshot<N>.<jpg|png>, for example com.nintendosss.spice_screenshot1.jpg; the first is the one an
app's artwork is made from. Each file is listed with its SHA-256, which is how Husk tells a changed picture from one it
already has.

Add or replace pictures in showcase/, run this, commit both, push. No new Husk release is needed.
"""
import hashlib
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent / "showcase"
NAME = re.compile(r"^(?P<package>[A-Za-z0-9_.]+)_screenshot(?P<n>\d+)\.(jpg|jpeg|png)$")


def main() -> None:
    apps: dict[str, list[dict]] = {}
    for path in sorted(ROOT.iterdir()):
        m = NAME.match(path.name)
        if not m:
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        apps.setdefault(m["package"], []).append(
            {"file": path.name, "sha256": digest, "size": path.stat().st_size, "n": int(m["n"])})
    for files in apps.values():
        files.sort(key=lambda f: f["n"])
        for f in files:
            del f["n"]
    index = {"format": 1, "apps": dict(sorted(apps.items()))}
    (ROOT / "index.json").write_text(json.dumps(index, indent=2) + "\n")
    print(f"{sum(len(v) for v in apps.values())} picture(s) for {len(apps)} app(s) -> {ROOT / 'index.json'}")


if __name__ == "__main__":
    main()
