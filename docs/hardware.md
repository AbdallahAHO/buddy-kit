# Hardware

## Boards

| Env | Board | Status |
|---|---|---|
| `waveshare-esp32c6-touch-amoled-2-16` | ESP32-C6, 480×480 AMOLED (SH8601), no PSRAM | **hardware-verified** — the board on the desk |
| `waveshare-esp32s3-touch-amoled-1-8` | ESP32-S3, 368×448 (SH8601), PSRAM | compile-verified |
| `waveshare-esp32s3-touch-amoled-1-75c` | round 466×466 (CO5300) | compile-only |
| `waveshare-esp32s3-touch-amoled-2-16` | 480×480 (CO5300) | compile-only |

The HAL pattern: `hal/boards/<board>.h` declares pins + ~20 capability flags
(`BOARD_HAS_PSRAM`, `BOARD_DISPLAY_PUSH_STREAMED`, `BOARD_KEY1_ACTIVE_HIGH`…);
`hal/hw/*` branches on flags; `pins.h` selects the header via the env's
`-DBOARD_*` define. Adding a board = one header + one ini env (see
extending.md). App code never mentions a board.

## Build, flash, test

```bash
cd apps/buddy
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload   # port + 460800 pinned in ini
pio device monitor                                          # 115200
python3 tools/test_serial.py                                 # drive personas over USB
```

C6 house rules (from the parent esp workspace): upload at **460800**; if
esptool can't connect, hold BOOT → tap PWR → release BOOT for download mode.
Full-chip recovery (factory snapshot) lives in the esp workspace:
`../../esp/boards/wsc6-amoled-2.16/board.py restore`.

## Partition table (`apps/buddy/ota_8mb.csv`) — DO NOT reshape casually

Why it's shaped this way: [ADR 005](decisions/005-nvs-ends-at-0xe000.md)
(NVS boundary) and [ADR 006](decisions/006-dual-ota-partition-table.md)
(dual-OTA slots, fixed data offsets).

```
nvs      0x9000  0x5000     ← must END at 0xe000
otadata  0xe000  0x2000     ← PlatformIO writes boot_app0.bin here on EVERY upload
ota_0    0x10000 0x300000   dual-OTA slots (see docs/ota.md)
ota_1    0x310000 0x300000
spiffs   0x610000 0x1F0000  (LittleFS: /characters/)
```

The single-slot `no_ota_8mb.csv` predates OTA; `ota_8mb.csv` (dual-slot) is
current. `nvs`/`spiffs` offsets are identical between them, so switching
tables over USB preserves creds, settings and characters.

History: upstream's table had NVS spanning 0x9000–0xf000. PlatformIO
unconditionally flashes `boot_app0.bin` at 0xe000, silently corrupting the
last NVS page on **every upload** — freshly written keys (Wi-Fi creds!)
vanished after reflashes. The current shape is the standard Arduino no-OTA
layout and is load-bearing. Upstream still has the bug (PR-worthy).

NVS namespace `"buddy"` holds everything: stats, settings (`s_*`), petname/
owner, species, `wssid`/`wpass`, `huburl`, `s_jig`. App-only uploads preserve it;
`factory reset` (or `pio run -t erase`) wipes it.

## C6 native-USB (HWCDC) quirks — field-verified

- **RX ring sized 2048** in setup(): native USB delivers a whole 370-byte
  chunk line faster than the loop drains; the 256-byte default (sized for
  real UARTs) silently dropped the tail.
- **~1 % of chunk ACKs drop at 4 KB flash-erase boundaries** during USB file
  push. The data IS written (byte counters prove it) — only the reply notify
  is lost while the single core erases. BLE transfers are unaffected.
  `test_xfer.py` notes this; check `file_end`'s `n` before assuming loss.
- **Opening the serial port DTR-resets the chip.** Wait ~2 s before the
  first command (the test tools do).

## Display + fonts

The C6 2.16" panel blanks if QSPI CS toggles between row writes — its push
is one continuous transaction (`BOARD_DISPLAY_PUSH_STREAMED`). Any TU that
includes Arduino_GFX **and uses U8g2 fonts** must define
`#define U8G2_FONT_SECTION(name)` *before* the GFX include (see
`screens/home.cpp` and `main.cpp`).

## Memory budget (C6 = the floor)

512 KB RAM, no PSRAM. Current build: ~27 % static RAM, ~109 KB heap free
with Wi-Fi + BLE + portal active. Canvas is ~80 KB. No heap in hot paths;
fixed buffers; the status ack reports live heap for monitoring.
