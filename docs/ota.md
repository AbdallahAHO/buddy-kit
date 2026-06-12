# Over-the-air updates

Two wireless paths, one partition layout. The first OTA-capable firmware
must be flashed once over USB (you can't OTA a single-slot device into a
dual-slot one); everything after that is wireless.

## The partition requirement

ESP32 OTA needs **two app slots** so the running firmware writes the new
image into the inactive one, then otadata flips which boots (with rollback
on a bad image). `apps/buddy/ota_8mb.csv`:

```
nvs      0x9000  0x5000     preserved across the switch (creds, settings)
otadata  0xe000  0x2000     boot-slot selector
ota_0    0x10000 0x300000   ~3MB app slot (image is ~2MB → headroom)
ota_1    0x310000 0x300000  ~3MB app slot
spiffs   0x610000 0x1F0000  preserved (LittleFS /characters/)
```

`nvs` and `spiffs` keep the offsets from the old single-slot table, so the
one-time USB reflash onto this layout **preserves Wi-Fi creds, settings and
installed characters** (verified). Recovery if a flash goes wrong:
`../../esp/boards/wsc6-amoled-2.16/board.py restore`.

## Path 1 — HTTP pull (the product path, `lib/ota`)

The device fetches a `.bin` from a URL and self-flashes. Driven by a command
over any transport, so the hub / desktop / a script can push updates:

```
{"cmd":"ota","url":"http://host:8088/firmware.bin"}
```

Flow: ack → download (progress screen) → write inactive slot → reboot into
it. The status ack reports the running slot (`ota.slot` = `ota_0`/`ota_1`),
which flips after a successful update. `otaPull` guards against a
single-slot table (fails loudly, never bricks) and an offline radio.

This is the **TRMNL/BYOS model** and the M3 target: a Cloudflare Worker
serves `firmware.bin` and the device pulls it. Current limit: plain `http://`
only — pulling from the Worker over `https://` needs `WiFiClientSecure`
(~40 KB heap + cert), the same TLS gap as `transport-http`.

Local test:

```bash
cd apps/buddy && pio run -e waveshare-esp32c6-touch-amoled-2-16
cd .pio/build/waveshare-esp32c6-touch-amoled-2-16 && python3 -m http.server 8088
# then, over serial/BLE/hub:
{"cmd":"ota","url":"http://<your-ip>:8088/firmware.bin"}
```

## Path 2 — ArduinoOTA push (the dev inner loop, `dev_ota.cpp`)

mDNS push so you stop unplugging during development. The device advertises
`<device-name>.local` (e.g. `Claude-D41A.local`) once Wi-Fi is up:

```bash
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload --upload-port 192.168.2.199
# or --upload-port Claude-D41A.local
```

~75 s for a 2 MB image over Wi-Fi. Same dual-slot mechanism underneath, so
it also rolls back a bad image. Product devices would gate or strip this;
it's on by default here because this is the active dev board.

## Which to use

| Situation | Path |
|---|---|
| Iterating on code at your desk | ArduinoOTA push (`-t upload --upload-port`) |
| Shipping an update to a device in the field | HTTP pull (`{"cmd":"ota"}` from the hub) |
| First flash / recovery | USB (`-t upload`) + `board.py restore` |

## Rollback

Both paths use the ESP-IDF OTA primitive: a freshly flashed slot boots once;
the app can mark itself valid (or the bootloader rolls back to the previous
slot on repeated boot failure). buddy-kit relies on the default reboot-on-
update behavior; an explicit `esp_ota_mark_app_valid_cancel_rollback()`
health gate is a future hardening step (M-future).
