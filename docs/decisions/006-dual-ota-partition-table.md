# 006: Dual-OTA partition table keeps nvs/spiffs offsets fixed

**Decision:** `ota_8mb.csv` is the current partition table — two ~3 MB app slots for wireless updates — and it deliberately keeps `nvs` and `spiffs` at the exact offsets of the old single-slot table.
**Status:** approved
**Date:** 2026-06-11

## Context

ESP32 OTA requires two app slots: the running firmware writes the new
image into the inactive slot, then otadata flips which one boots (with
rollback on a bad image). The original `no_ota_8mb.csv` had one big app
slot — wireless updates were impossible, and every update meant USB.

Moving to dual slots forces a one-time USB reflash (you can't OTA a
single-slot device into a dual-slot layout). That reflash must not cost
the user their Wi-Fi creds, settings, and installed characters.

## Decision

Adopt `ota_8mb.csv`: `ota_0`/`ota_1` at 0x10000/0x310000 (~3 MB each;
image is ~2 MB → headroom), with `nvs` (0x9000) and `spiffs` (0x610000)
at offsets identical to the old table. Because the data partitions don't
move, switching tables over USB preserves everything (verified on the
C6). App slot layout is now load-bearing: reshaping it breaks OTA.

## Consequences

Both wireless paths work — HTTP pull (`lib/ota`, the product path) and
ArduinoOTA push (the dev loop) — with bootloader rollback underneath.
The cost is the frozen layout: future partition wants (bigger spiffs,
a factory slot) must keep `nvs`/`spiffs` fixed or accept a creds-wiping
migration. The how lives in `docs/ota.md`; the table and its history in
`docs/hardware.md`.

## Related

- [005](005-nvs-ends-at-0xe000.md) — the NVS boundary both tables share.
