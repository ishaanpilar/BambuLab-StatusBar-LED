#!/usr/bin/env python3
"""Stages firmware binaries + a manifest for ESP Web Tools.

Copies the binaries a `pio run -e esp32dev` build already produces, plus
the Arduino ESP32 framework's boot_app0.bin (needed by the default OTA-aware
partition table), into web-installer/firmware/, and writes manifest.json
with the flash offsets ESP Web Tools needs.

Usage (after `pio run -e esp32dev` from firmware/):
    python3 tools/prepare_web_installer.py [--version VERSION]
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = FIRMWARE_DIR.parent
BUILD_DIR = FIRMWARE_DIR / ".pio" / "build" / "esp32dev"
OUTPUT_DIR = REPO_ROOT / "web-installer" / "firmware"

# (source file, dest filename, flash offset)
PARTS = [
    ("bootloader.bin", "bootloader.bin", 0x1000),
    ("partitions.bin", "partitions.bin", 0x8000),
    ("firmware.bin", "firmware.bin", 0x10000),
]
BOOT_APP0_OFFSET = 0xE000


def find_boot_app0() -> Path:
    candidates = list(Path.home().glob(
        ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    ))
    if not candidates:
        sys.exit(
            "Could not find boot_app0.bin under ~/.platformio/packages/"
            "framework-arduinoespressif32/tools/partitions/ — run `pio run -e esp32dev` first."
        )
    return candidates[0]


def default_version() -> str:
    version_file = REPO_ROOT / "VERSION"
    if version_file.exists():
        version = version_file.read_text().strip()
        if version:
            return version

    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=REPO_ROOT,
        capture_output=True, text=True, check=False,
    )
    return result.stdout.strip() or "dev"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default=None)
    args = parser.parse_args()

    if not BUILD_DIR.exists():
        sys.exit(f"{BUILD_DIR} not found — run `pio run -e esp32dev` from firmware/ first.")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    manifest_parts = []
    for src_name, dest_name, offset in PARTS:
        src = BUILD_DIR / src_name
        if not src.exists():
            sys.exit(f"Expected build output missing: {src}")
        shutil.copy(src, OUTPUT_DIR / dest_name)
        manifest_parts.append({"path": dest_name, "offset": offset})

    boot_app0 = find_boot_app0()
    shutil.copy(boot_app0, OUTPUT_DIR / "boot_app0.bin")
    manifest_parts.append({"path": "boot_app0.bin", "offset": BOOT_APP0_OFFSET})
    manifest_parts.sort(key=lambda p: p["offset"])

    manifest = {
        "name": "Bambu Status Bar",
        "version": args.version or default_version(),
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32", "parts": manifest_parts}],
    }
    (OUTPUT_DIR / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"Wrote {OUTPUT_DIR} (version {manifest['version']})")


if __name__ == "__main__":
    main()
