# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

**mag-usb** is a portable C command-line utility that reads a PNI
RM3100 3-axis magnetometer (plus an MCP9808 ambient temperature
sensor) via a Pololu 5396 / 5397 USB-to-I²C adapter. It emits one
JSON line per UTC second on stdout: timestamp, raw counts, scaled
field, and temperature.

This is **Dave Witten's upstream project**, not an AC0G original. The
checkout here under `/opt/git/sigmond/mag-usb/` carries AC0G's
fork-prep work (PRs #1–#4 merged) on top of upstream
[`wittend/mag-usb`](https://github.com/wittend/mag-usb). Treat the
upstream `master` as authoritative; this checkout exists so
mag-recorder's `install.sh` can build it from a known-good source
without depending on an external clone.

Part of the HamSCI sigmond suite as a **build-time dependency**, not
a sigmond client — see `/opt/git/sigmond/mag-recorder/CLAUDE.md` for
the Python wrapper that owns the contract surface, JSONL spool, and
PSWS upload path.

## Authors / provenance

- **Upstream:** Dave Witten (`wittend`), GPL-3.0.
  Upstream repo: https://github.com/wittend/mag-usb
- **This fork:** Michael Hauan (AC0G / `mijahauan`). PRs #1–#4 land
  here first and get opened against upstream:
  - PR #1 — `rm3100` scaling correctness.
  - PR #2 — `-f <config>` and `-A <hex addr>` CLI overrides.
  - PR #3 — `-P` reads back chip CC / NOS / TMRC / REVID and flags
    mismatches.
  - PR #4 — `extern int` vs `uint` type mismatch on `GAIN_150` (UB
    on read).
- See `mag-recorder/docs/PROVENANCE.md` for the broader collaboration
  story and license analysis.

## Layout

This is a C project, not Python:

```
src/             # main.c, rm3100.{c,h}, i2c-pololu.{c,h}, magdata.c,
                 # sensor_tests.{c,h}, cmdmgr.h, ws_bridge.h, …
tests/           # test_i2c_pololu.c, test_websocket.cpp
tools/           # ws_client.cpp + helpers
assets/          # standalone reference code (timer / ctrl-C examples)
docs/            # mkdocs source — Configuration.md, Orientation-and-Axes.md,
                 # building.md, …  Published to mag-usb.readthedocs.io
install/         # 99-PololuI2C.rules (udev), other host-side files
third_party/     # vendored header-only deps (the WebSocket library)
CMakeLists.txt   # build system
mag_usb_build.mk # auxiliary make include
requirements.txt # docs-build deps (mkdocs etc.) — NOT runtime deps
```

There is no `pyproject.toml`. The "Python pieces" (`requirements.txt`)
exist only to build the `mkdocs` documentation site.

## Commands

```bash
# Build (CMake; WebSocket bridge built in by default — needs C++11 compiler)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mag-usb

# Pure-C build (omit WebSocket bridge):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_WEBSOCKET=OFF
cmake --build build --target mag-usb

# Quick checks
./build/mag-usb -Q                  # adapter presence verification
./build/mag-usb -P                  # print current chip + config state
./build/mag-usb -f /etc/mag-usb/config.toml   # run with explicit config

# Install udev rule once per host so the adapter shows up as /dev/ttyMAG0
sudo cp install/99-PololuI2C.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Key design facts

- **Portable C, minimal dependencies.** The WebSocket bridge vendors
  a header-only C++11 library under `third_party/`; otherwise no
  external libs. Builds on Debian/Ubuntu-class Linux.
- **Windows not supported.** macOS may work but is untested.
- **Pololu Isolated USB-to-I²C** family is the target adapter (the
  isolation matters for the long PSWS antenna cable runs).
- **JSON-on-stdout is the contract.** Anything consuming mag-usb
  (notably `mag-recorder/core/supervisor.py`) reads one JSON line
  per second on stdout:
  `{"ts": "DD Mon YYYY HH:MM:SS", "rt": ..., "x": ..., "y": ..., "z": ...}`.
  Changing this format is breaking; coordinate with `mag-recorder`
  if you do.
- **Config search order:**
  1. `-f <path>` on the CLI (missing/unreadable is a hard error).
  2. `/etc/mag-usb/config.toml`.
  3. `config.toml` in the current working directory.
  4. Built-in defaults.

  `mag-recorder` writes `/etc/mag-usb/config.toml` from its own
  `[mag]` section via `mag_recorder.core.driver_config`. Don't
  hand-edit the rendered file — re-run `smd config edit mag-recorder`
  or restart `mag-recorder@<id>.service` to regenerate it.

## Relationship to the rest of the sigmond suite

- **mag-recorder** is the Python supervisor that consumes mag-usb's
  stdout and owns the sigmond client contract. mag-usb itself has
  no contract surface.
- **mag-recorder's `install.sh`** builds mag-usb from this checkout
  (and falls back to cloning the upstream if absent).
- **No editable-install relationship** — this is a compiled binary,
  not a Python sibling. Re-build via CMake when the source changes;
  the binary's location lives in mag-recorder's config.

## What this CLAUDE.md is NOT

- Not a substitute for upstream docs at https://mag-usb.readthedocs.io —
  the configuration keys, orientation transforms, and JSON schema
  live there.
- Not a sigmond client briefing — go to `mag-recorder/CLAUDE.md`
  for the contract / spool / upload story.
- Not a comprehensive C-style guide for this repo — match what's
  already there; this is upstream's code, not ours to restructure.
