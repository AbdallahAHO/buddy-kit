# Contributing

buddy-kit is a lego-layered ESP32 firmware monorepo. The full, machine-readable
rules an agent (or you) should follow live in [AGENTS.md](AGENTS.md) — read it
first. This file is the human quickstart.

## Setup

- **PlatformIO Core** (`pip install platformio`) — firmware build / flash / test.
- **Node + pnpm** — only for `cloud/` (the Cloudflare Worker).
- The **Waveshare ESP32-C6 Touch AMOLED 2.16"** is the hardware-verified board.

## Build, test, flash

```bash
pio run  -d apps/buddy -e waveshare-esp32c6-touch-amoled-2-16            # build
pio run  -d apps/buddy -e waveshare-esp32c6-touch-amoled-2-16 -t upload  # flash
pio test -e native_test                                                  # host unit tests
```

No toolchain? Flash from the browser — see [docs/flashing.md](docs/flashing.md).

## The rules that matter

- **Docs are part of the change.** Touch a documented behavior → update its doc
  in the *same commit* (the contract table in [AGENTS.md](AGENTS.md)).
- **Material decisions get an ADR** in [docs/decisions/](docs/decisions/README.md),
  same commit. The `adr` skill walks you through it.
- **Conventional Commits**, atomic, imperative subject ≤72 chars.
- **The six rules** (dependencies point down · one-way flow · the ghost rule ·
  one place each · heapless contracts · acks fan out) are bugs when violated.
- New firmware builds green on all four board envs (CI enforces); C6 changes get
  flashed and exercised before commit.

## Layout

`apps/` compositions · `lib/` reusable legos · `hal/` board layer ·
`blocks/` copy-in screens · `cloud/` fleet Worker · `web/` browser flasher ·
`docs/` the *why* (architecture, protocol, decisions).
