# 013: Distribute via a browser web flasher (ESP Web Tools), binaries in Releases not git

**Decision:** The kit installs from a static web page using ESP Web Tools over Web Serial. Each app ships as a **multi-part manifest** (the four flash parts at their real offsets, not one merged blob), firmware binaries live as GitHub Release assets (never committed), and the page hosts on GitHub Pages. After flashing, the same page provisions Wi-Fi and shows the live screen over our own JSON line protocol.
**Status:** approved
**Date:** 2026-06-13

## Context

Time-to-first-pixel was the kit's biggest adoption gap: using it meant cloning,
installing PlatformIO, and wiring a board. The sibling project RSVP Nano showed
the answer — a hosted ESP Web Tools flasher — and that distribution layer is
exactly what we lacked. The open question was the manifest shape: a single
merged image (RSVP Nano's choice) 0xFF-pads the gaps, which would write over
our NVS region (0x9000–0xe000) and wipe Wi-Fi creds on every flash.

## Decision

Multi-part manifest: `bootloader@0x0`, `partitions@0x8000`, `boot_app0@0xe000`,
`firmware@0x10000` (C6 offsets, esptool-authoritative). esptool-js writes each
part at its offset and skips the gaps, so NVS (creds) and spiffs (characters)
are preserved — web-flashing behaves like `pio upload`, honoring
[ADR 005](005-nvs-ends-at-0xe000.md)/[006](006-dual-ota-partition-table.md).
`new_install_prompt_erase: true` — counterintuitive but verified against the
ESP Web Tools source: with `improv: false` it can't detect "same firmware", so
`false` would *force* a full-chip erase, while `true` shows an **unchecked**
erase box whose default (no erase) keeps NVS + spiffs; ticking it does a clean
wipe. `improv: false` — the firmware speaks our JSON line protocol, not Improv,
so steps 3–4 (Wi-Fi + the live framebuffer via the
[ADR 011](011-virtual-display-is-a-framebuffer-tee.md) vdp tee) use our own
Web Serial code, not ESP Web Tools provisioning. `tools/export_web_flasher.py`
builds the parts + manifest; binaries are gitignored and fetched from Releases
by the Pages workflow (dormant, per [ADR 010](010-dormant-ci-workflow.md)).

## Consequences

Plug in the C6, open the page, flash, watch the pet boot — no toolchain. The
picker is data-driven (`web/apps.json`), so a new composition appears in the
flasher by adding an entry. Hardware-verified: the exact served bytes boot and, written without erase,
preserve creds (Wi-Fi stayed joined). Costs: a CDN dep
(ESP Web Tools, pinned), Web Serial means Chrome/Edge desktop only, and the
merged-vs-multipart choice is load-bearing — never collapse to a merged blob
without re-confirming the NVS gap. The how lives in `docs/flashing.md`.
