#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Upload existing installers matching an independently reviewed draft checksum.

This does not build or publish a release. The draft, its source commit and its
SHA256SUMS.txt must already exist, following local inspection of the installers.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def api(path):
    return json.loads(subprocess.check_output(["gh", "api", path]))


def main():
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["HUSK_BUILD_RUN_ID"]
    tag = os.environ["HUSK_RELEASE_TAG"]
    assert run_id.isdecimal() and re.fullmatch(r"v\d+\.\d+\.\d+", tag)
    run = api(f"repos/{repo}/actions/runs/{run_id}")
    release = api(f"repos/{repo}/releases/tags/{tag}")
    assert run["conclusion"] == "success" and run["status"] == "completed", "build did not succeed"
    assert release["draft"] and not release["prerelease"], "upload only to an existing draft"
    assert release["target_commitish"] == run["head_sha"], "draft and build source commits differ"
    root = Path("release-files")
    subprocess.run(["gh", "release", "download", tag, "--repo", repo, "--pattern", "SHA256SUMS.txt",
                    "--dir", str(root)], check=True)
    expected = {}
    for line in (root / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        digest, name = line.split(maxsplit=1)
        assert re.fullmatch(r"[0-9a-f]{64}", digest) and Path(name).name == name
        assert name.endswith((".ipa", ".tipa")) and name not in expected
        expected[name] = digest
    files = sorted(p for p in root.iterdir() if p.suffix in (".ipa", ".tipa"))
    assert {p.name for p in files} == set(expected) and {p.suffix for p in files} == {".ipa", ".tipa"}
    for path in files:
        with path.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        report = json.loads(path.with_suffix(path.suffix + ".verification.json").read_text(encoding="utf-8"))
        assert actual == expected[path.name] == report["sha256"], path.name
        assert report["version"] == tag[1:] and report["bytes"] == path.stat().st_size
        commit = report["commit"].split("-")[0]
        assert len(commit) >= 7 and run["head_sha"].startswith(commit)
        assert all(report[k] == "passed" for k in ("archive_crc", "chinese_metadata_and_ui_string", "required_resources"))
        current = api(f"repos/{repo}/releases/{release['id']}")
        assert current["draft"] and current["target_commitish"] == run["head_sha"]
        existing = next((a for a in current["assets"] if a["name"] == path.name), None)
        if existing and existing["state"] == "uploaded" and existing.get("digest") == "sha256:" + actual:
            print("Already uploaded with matching SHA-256:", path.name, flush=True)
            continue
        command = ["gh", "release", "upload", tag, str(path), "--repo", repo]
        if existing:
            command.append("--clobber")
        subprocess.run(command, check=True)
        current = api(f"repos/{repo}/releases/{release['id']}")
        uploaded = next(a for a in current["assets"] if a["name"] == path.name)
        assert uploaded["state"] == "uploaded" and uploaded["digest"] == "sha256:" + actual
        assert uploaded["size"] == path.stat().st_size
        print("Uploaded and verified:", path.name, actual, flush=True)


if __name__ == "__main__":
    main()
