---
name: buddy-build-flash-verify
description: Build, flash, and verify buddy-kit firmware on the ESP32-C6/S3 boards — pio run envs, serial probing protocol, DTR-reset trap, chunk-ack quirk, panic triage, recovery. Use when building firmware, flashing a board, probing over serial, decoding a crash/backtrace, or when esptool/upload fails.
---

# buddy: build, flash, verify

Working directory: `apps/buddy`. Hardware truth: `../../docs/hardware.md`.

## Envs

| Env | Board | Status |
|---|---|---|
| `waveshare-esp32c6-touch-amoled-2-16` | ESP32-C6, no PSRAM | **the board on the desk — default** |
| `waveshare-esp32s3-touch-amoled-1-8` | ESP32-S3, PSRAM | compile-verified |
| `waveshare-esp32s3-touch-amoled-1-75c` | round CO5300 | compile-only |
| `waveshare-esp32s3-touch-amoled-2-16` | 480×480 CO5300 | compile-only |

Port and baud (460800 for the C6) are pinned in `platformio.ini`.

## The loop

```bash
cd apps/buddy
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload
# wait for reboot, then probe:
python tools/test_serial.py      # personas via USB
python tools/test_xfer.py        # file push protocol
```

Build-green is not done — flash and exercise on the C6 before commit.
UI changes additionally need the physical checklist in
`../../docs/extending.md` (Testing changes).

## Serial probing protocol

- 115200 baud. **Opening the port DTR-resets the chip** — wait ~2 s after
  open before the first command (the test tools do this).
- Probe with `{"cmd":"status"}\n` — the ack reports fw version, heap,
  wifi state, and the running OTA slot.
- Prefer `tools/test_serial.py` / `test_xfer.py` over hand-rolled probes;
  they already handle the reset wait and framing.

## TRAPS

- **Never send `{"cmd":"wifi","forget":true}` while probing.** It wipes
  the stored creds; re-pairing needs the QR portal flow.
- **No `echo > $PORT` loops.** Every open DTR-resets the chip — a shell
  loop reboots the device once per line and you probe a booting chip.
- **~1 % of chunk acks drop at 4 KB flash-erase boundaries** during USB
  file push. The data IS written — check `file_end`'s `n` before assuming
  loss. BLE transfers are unaffected.
- If esptool can't connect: hold BOOT → tap PWR → release BOOT for
  download mode.

## Crash / panic?

See [references/panic-triage.md](references/panic-triage.md).

## Recovery

Full-chip factory restore lives in the sibling esp workspace:
`../esp/boards/wsc6-amoled-2.16/board.py restore` (run from the repo
parent). It only needs esptool, not PlatformIO.
