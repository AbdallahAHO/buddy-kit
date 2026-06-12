# 005: NVS ends at 0xe000 so PlatformIO's boot_app0 write lands in otadata

**Decision:** The NVS partition is 0x9000–0xe000 (0x5000 long) — exactly the standard Arduino layout — because PlatformIO unconditionally flashes `boot_app0.bin` at 0xe000 on every upload.
**Status:** approved
**Date:** 2026-06-11

## Context

Upstream's partition table gave NVS 0x9000–0xf000. PlatformIO writes
`boot_app0.bin` at 0xe000 on **every** upload, silently corrupting the
last NVS page — freshly written keys (Wi-Fi creds!) vanished after
reflashes. The failure was maddening to trace because the corruption is
silent and only hits keys that landed in the final page. Upstream still
carries the bug.

## Decision

Shrink NVS to end at 0xe000 and put `otadata` there (0xe000, 0x2000) —
the standard Arduino layout, which is precisely what boot_app0.bin is
*meant* to overwrite. Both partition tables (`no_ota_8mb.csv`,
`ota_8mb.csv`) share this shape. The offsets are load-bearing: do not
reshape the table without re-reading this ADR and `docs/hardware.md`.

## Consequences

Uploads stopped eating Wi-Fi creds; NVS namespace `buddy` (stats,
settings, creds, hub URL) survives every reflash. NVS is 0x1000 smaller
than upstream's, which is irrelevant at current usage. The fix is
PR-worthy upstream. Mechanics and the full table live in
`docs/hardware.md`; the OTA view of the same layout in `docs/ota.md`.

## Related

- [006](006-dual-ota-partition-table.md) — the dual-slot table built on the same fixed offsets.
