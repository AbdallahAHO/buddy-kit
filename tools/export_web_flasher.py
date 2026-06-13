#!/usr/bin/env python3
"""Build the per-app flash artifacts the browser flasher serves.

For each composition (buddy, glance) this runs `pio run`, then copies the four
flash parts into web/firmware/<app>/ and writes an ESP Web Tools manifest.json.

Why four separate parts instead of one merged blob: esptool-js writes each part
at its real offset and leaves the gaps alone, so the NVS region (0x9000-0xe000)
is never touched — web-flashing preserves Wi-Fi creds + characters, same as
`pio upload`. A merged blob would 0xFF-pad over NVS and wipe them (ADR 005/006).

Usage:
    python3 tools/export_web_flasher.py [--app buddy|glance|all] [--version V] [--skip-build]
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB_FIRMWARE = ROOT / "web" / "firmware"

ENV = "waveshare-esp32c6-touch-amoled-2-16"
CHIP_FAMILY = "ESP32-C6"

# (flash offset, build-artifact filename). Offsets are authoritative for the
# C6 on the pioarduino platform: bootloader 0x0 (esptool ESP32C6ROM), partition
# table 0x8000 (chip fixed), boot_app0/otadata 0xe000 (ADR 005), app 0x10000.
PARTS = [
    (0x0000, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, "boot_app0.bin"),
    (0x10000, "firmware.bin"),
]
OTA_SLOT_SIZE = 0x300000  # ota_0 size from ota_8mb.csv — firmware must fit

APPS = {
    "buddy": {"display": "buddy-kit · Buddy", "dir": "apps/buddy"},
    "glance": {"display": "buddy-kit · Glance", "dir": "apps/glance"},
}


def pio() -> str:
    """Resolve the PlatformIO CLI (installed as a uv tool on this machine)."""
    for candidate in (
        Path.home() / ".local/share/uv/tools/platformio/bin/pio",
        Path.home() / ".platformio/penv/bin/pio",
    ):
        if candidate.exists():
            return str(candidate)
    found = shutil.which("pio")
    if found:
        return found
    raise SystemExit("PlatformIO not found (looked for the uv tool, penv, then PATH).")


def git_version() -> str:
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, text=True
        ).strip()
        return v or "dev"
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "dev"


def boot_app0() -> Path:
    hits = sorted(Path.home().glob(
        ".platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin"
    ))
    if not hits:
        raise SystemExit("boot_app0.bin not found — build at least once so the framework is installed.")
    return hits[-1]


def export_app(app: str, version: str, skip_build: bool) -> dict:
    spec = APPS[app]
    app_dir = ROOT / spec["dir"]
    build_dir = app_dir / ".pio" / "build" / ENV
    out_dir = WEB_FIRMWARE / app
    out_dir.mkdir(parents=True, exist_ok=True)

    if not skip_build:
        print(f"+ pio run -d {spec['dir']} -e {ENV}")
        subprocess.run([pio(), "run", "-d", str(app_dir), "-e", ENV], check=True)

    # Resolve each part's source, copy it into web/firmware/<app>/, validate fit.
    sources = {
        "bootloader.bin": build_dir / "bootloader.bin",
        "partitions.bin": build_dir / "partitions.bin",
        "boot_app0.bin": boot_app0(),
        "firmware.bin": build_dir / "firmware.bin",
    }
    placed = []  # (offset, name, size)
    for offset, name in PARTS:
        src = sources[name]
        if not src.exists():
            raise SystemExit(f"{app}: missing flash part {name} at {src}")
        shutil.copy2(src, out_dir / name)
        placed.append((offset, name, src.stat().st_size))

    # Fail loud if any part overruns the next part's offset (or the app slot).
    placed.sort(key=lambda p: p[0])
    for i, (offset, name, size) in enumerate(placed):
        ceiling = placed[i + 1][0] if i + 1 < len(placed) else offset + size
        if name == "partitions.bin":
            ceiling = 0x9000  # must not grow into the NVS region (0x9000-0xe000)
        if name == "firmware.bin":
            ceiling = offset + OTA_SLOT_SIZE  # firmware fills its own slot
        if offset + size > ceiling:
            raise SystemExit(
                f"{app}: {name} ({size} B at {hex(offset)}) overruns {hex(ceiling)} — bad offsets"
            )

    # The OTA image is just the app binary (what lib/ota pulls into the spare slot).
    shutil.copy2(build_dir / "firmware.bin", out_dir / "ota.bin")

    manifest = {
        "name": spec["display"],
        "version": version,
        # ESP Web Tools inverts intuition (verified against its install-dialog
        # source): with improv:false a fresh install ERASES the whole chip when
        # this is false. Setting it true shows an *optional* erase checkbox,
        # unchecked by default → no erase → our parts are written at their
        # offsets and NVS (creds) + spiffs (characters) survive, like
        # `pio upload`. The user can tick the box for a clean wipe. See ADR 013.
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": CHIP_FAMILY,
            "improv": False,  # firmware speaks our JSON line protocol, not Improv
            "parts": [{"path": name, "offset": offset} for offset, name in PARTS],
        }],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    fw = (out_dir / "firmware.bin").stat().st_size
    print(f"  {app}: 4 parts + ota.bin, firmware {fw // 1024} KB → {out_dir}")
    return {"id": app, "version": version, "firmwareBytes": fw}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", choices=[*APPS, "all"], default="all")
    parser.add_argument("--version", default=None, help="manifest version string (default: git describe)")
    parser.add_argument("--skip-build", action="store_true", help="reuse existing .pio output")
    args = parser.parse_args()

    version = args.version or git_version()
    targets = list(APPS) if args.app == "all" else [args.app]
    WEB_FIRMWARE.mkdir(parents=True, exist_ok=True)

    for app in targets:
        export_app(app, version, args.skip_build)
    print(f"Web flasher firmware exported to {WEB_FIRMWARE} (version {version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
