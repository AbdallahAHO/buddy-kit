# 002: The C6's 512 KB heap is the floor: heapless contracts, no std::function

**Decision:** All shared code is written to the weakest target — the ESP32-C6 with 512 KB RAM and no PSRAM: no `std::function`, no `std::vector`, fixed-size buffers, no heap in hot paths.
**Status:** approved
**Date:** 2026-06-10

## Context

The board matrix spans PSRAM-equipped S3s and the PSRAM-less, single-core
C6 — which happens to be the hardware-verified board on the desk. With
Wi-Fi + BLE + the captive portal all active, the C6 sits around ~109 KB
free heap; the canvas alone is ~80 KB. A `std::function` capture here, a
growing `std::vector` there, and the radio stacks start failing
allocations in ways that look like driver bugs.

Writing `lib/*` to the comfortable S3 budget would mean every lego carries
a hidden "PSRAM required" footnote, and the kit's reference board couldn't
run its own firmware.

## Decision

The C6 sets the bar for everything in `lib/` and `hal/`. Contracts are
C-style: structs of plain function pointers (`ByteSource`, `FileSink`) or
link-time symbols (ADR 001). Buffers are fixed-size and sized explicitly
(e.g. the 2048-byte USB RX ring). Hot paths — the loop, render, transport
pumps — never allocate. `lib/agent-state` and `lib/line-bus` additionally
stay Arduino-free so they compile under PlatformIO's `native` platform.

## Consequences

Memory behavior is predictable enough to budget in a table
(`docs/hardware.md`); the status ack reports live heap so regressions show
up in routine probing. Code is more verbose than idiomatic C++ — every
buffer size is a decision. Boards with PSRAM leave headroom on the table,
which is the acceptable price for one codebase that runs everywhere.

## When to revisit

If the C6 is ever dropped from the matrix, the floor moves — revisit
per-lib, not globally.
