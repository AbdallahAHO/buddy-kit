#!/usr/bin/env python3
"""Populate web/firmware/ from the latest GitHub Release (used by the Pages CI).

The release workflow builds the flasher artifacts and uploads them as a single
`web-firmware.tar.gz` asset (the whole web/firmware/ tree — both apps, all
parts, manifests). This downloads that asset and extracts it, so the Pages site
always serves the latest released firmware without committing binaries to git.

Usage:  python3 tools/fetch_release_firmware.py --repo owner/name
"""
from __future__ import annotations

import argparse
import io
import json
import os
import tarfile
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
ASSET = "web-firmware.tar.gz"


def headers() -> dict:
    h = {"Accept": "application/vnd.github+json", "User-Agent": "buddy-kit-flasher"}
    token = os.environ.get("GITHUB_TOKEN", "").strip()
    if token:
        h["Authorization"] = f"Bearer {token}"
    return h


def get(url: str) -> bytes:
    req = urllib.request.Request(url, headers=headers())
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.read()
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"GitHub request failed (HTTP {exc.code}): {url}") from exc


def latest_release_or_none(repo: str):
    """Return the latest release JSON, or None if the repo has no release yet.
    Distinguishes 'no release' (soft, the site still deploys firmware-less) from
    real errors (rate-limit, outage, auth) which must fail the deploy loudly."""
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    req = urllib.request.Request(url, headers=headers())
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return None
        raise SystemExit(f"GitHub request failed (HTTP {exc.code}): {url}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    args = parser.parse_args()
    if not args.repo:
        raise SystemExit("Pass --repo owner/name (or set GITHUB_REPOSITORY).")

    release = latest_release_or_none(args.repo)
    if release is None:
        print("No published release yet — deploying the site without firmware.")
        return 0
    tag = release.get("tag_name", "?")
    asset = next((a for a in release.get("assets", []) if a.get("name") == ASSET), None)
    if not asset:
        raise SystemExit(f"Release {tag} has no {ASSET} asset.")

    print(f"Fetching {ASSET} from {tag}")
    blob = get(asset["browser_download_url"])
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tar:
        tar.extractall(WEB, filter="data")  # archive root is firmware/; filter blocks path traversal
    print(f"web/firmware populated from release {tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
