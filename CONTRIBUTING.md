# Contributing

Thanks for helping improve the Bambu Status Bar! A few guidelines:

- Open an issue before big changes/feature work.
- Fork → create a feature branch → push → open a PR.
- Use clear commit messages (one-line summary, optional body).
- For firmware changes:
  - Add/update tests in `firmware/test/test_printer_state.cpp` when you
    touch `firmware/src/PrinterState.cpp` — it's pure logic and runs on the
    host with `pio test -e native`, no hardware needed.
  - Regenerate the Arduino IDE mirror if you touch anything in
    `firmware/src/`: `python3 firmware/tools/generate_arduino_sketch.py`.
    CI (`.github/workflows/firmware-ci.yml`) fails if it's stale.
  - Update `firmware/README.md` / `docs/troubleshooting.md` if setup steps
    change.
- For hardware changes, update `hardware/README.md` and `hardware/BOM.md`
  to match.

Labeling:

- `bug` for reproducible problems
- `enhancement` for new features
- `help wanted` when maintainers request assistance
