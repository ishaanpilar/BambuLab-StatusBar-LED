#!/usr/bin/env python3
"""Concatenates firmware/src/*.{h,cpp} into a single Arduino IDE sketch.

PlatformIO (firmware/platformio.ini) is the source of truth; this script
mirrors that same source into a single .ino file for people who prefer the
Arduino IDE. Run it after any change under src/, or let `pio run -t
check-arduino-sketch` (used in CI) tell you the mirror is stale.

Usage:
    python3 tools/generate_arduino_sketch.py [--check]

--check: don't write the file, just exit non-zero if the generated content
would differ from what's currently committed (used by CI).
"""
import argparse
import re
import sys
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
SRC_DIR = FIRMWARE_DIR / "src"
OUTPUT = FIRMWARE_DIR / "arduino-ide" / "BambuStatusBar" / "BambuStatusBar.ino"

# Dependency order: a file must come after everything it depends on, since
# the result is one translation unit read top to bottom.
FILE_ORDER = [
    "Config.h",
    "PrinterState.h",
    "PrinterState.cpp",
    "LedController.h",
    "LedController.cpp",
    "BambuMqttClient.h",
    "BambuMqttClient.cpp",
    "Provisioning.h",
    "Provisioning.cpp",
    "main.cpp",
]

PRAGMA_ONCE_RE = re.compile(r"^\s*#pragma once\s*$")
LOCAL_INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"\s*$')
LIBRARY_INCLUDE_RE = re.compile(r"^\s*#include\s+<([^>]+)>\s*$")


def generate() -> str:
    local_names = set(FILE_ORDER)
    library_includes = []
    body_sections = []

    for filename in FILE_ORDER:
        path = SRC_DIR / filename
        lines = path.read_text().splitlines()
        body_lines = []
        for line in lines:
            if PRAGMA_ONCE_RE.match(line):
                continue
            local_match = LOCAL_INCLUDE_RE.match(line)
            if local_match:
                continue  # already inlined by concatenation
            lib_match = LIBRARY_INCLUDE_RE.match(line)
            if lib_match:
                if lib_match.group(1) not in library_includes:
                    library_includes.append(lib_match.group(1))
                continue
            body_lines.append(line)

        section = "\n".join(body_lines).strip("\n")
        body_sections.append(f"// ==== {filename} ====\n{section}\n")

    header = (
        "// GENERATED FILE — do not edit directly.\n"
        "// Source of truth is firmware/src/*.{h,cpp}; regenerate with:\n"
        "//   python3 firmware/tools/generate_arduino_sketch.py\n\n"
    )
    includes = "\n".join(f"#include <{name}>" for name in library_includes)
    return header + includes + "\n\n" + "\n".join(body_sections)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    generated = generate()

    if args.check:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != generated:
            print(f"{OUTPUT} is out of date. Run: python3 tools/generate_arduino_sketch.py")
            sys.exit(1)
        print("Arduino sketch mirror is up to date.")
        return

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generated)
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()
