# buddy-kit

A lego-layered monorepo for ESP32 companion devices, extracted from
[claude-desktop-buddy-esp32](https://github.com/vthinkxie/claude-desktop-buddy-esp32)
(MIT, itself a port of anthropics/claude-desktop-buddy). The buddy app is the
first composition; the layers are reusable for any desktop-to-gadget bridge
(e-paper dashboards, HTTP/Worker-fed status displays, …).

**Full documentation: [docs/](docs/README.md)** — architecture, rendering
tree, data flow, input, protocol, connectivity, hardware, extending recipes.
Contribution rules (incl. the docs-stay-true contract): [AGENTS.md](AGENTS.md).

## Layers

```
apps/buddy          the composition root (~470-line main.cpp)
  apps/buddy/src/app_state.h     the store: shared state, one header
  apps/buddy/src/screens/        organisms: home/info/pet/passkey/wifi — explicit canvas,
                                 screen-local state behind tiny APIs
  apps/buddy/src/overlays.cpp    menu/settings/reset as data tables → one ListPanel
  apps/buddy/src/input_router.cpp  raw input → focus ladder (approval > wifi > overlay > screen)
  lib/agent-state   AgentState schema + snapshot apply + persona selector (pure C++)
  lib/line-bus      ByteSource pipe contract, newline framer, ack fan-out, cmd dispatch
  lib/transport-ble NUS BLE bridge (NimBLE for C6, Bluedroid for S3, via -D flag)
  lib/transport-usb USB CDC as a ByteSource
  lib/file-push     desktop folder-push protocol, storage policy via FileSink
  lib/wifi-link     Wi-Fi provisioning: QR-joinable SoftAP + captive portal + join lifecycle
  lib/hid-mouse     BLE HID mouse on the shared GATT server (jiggler mode)
  lib/ota           wireless firmware update (HTTP pull, dual-OTA slots)
  lib/transport-http  hub polling as a ByteSource (GET /poll, POST /push) over wifi-link
  lib/faces         18 ASCII species + GIF character player (7-state persona contract)
  hal/hw            display/input/power/imu/rtc/audio, stateless, board-flag driven
  hal/boards        one capability header per board, selected via -DBOARD_*
  vendor/           upstream-vendored driver libs (ES8311, XCA9554, DriveBus)
```

Dependency rule: everything points down, nothing points up. App-policy stays
in the app (`app_commands.h`, `agent_link.h`, `faces_store.cpp` — single-TU
glue headers); libs receive policy via injected contracts (FileSink,
facesSpeciesLoad/Save, agentStateRandom).

## Build & flash

```bash
cd apps/buddy
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload   # the C6 on the desk
```

Envs exist for all four Waveshare AMOLED boards; the C6 is the verified one.
BLE stack and board are selected per env via `-DBUDDY_BLE_*` / `-DBOARD_*`.

## Protocol testing without the desktop app

```bash
cd apps/buddy
python tools/test_serial.py    # drive persona states over USB
python tools/test_xfer.py      # stream a character (GIF set) over USB
```

Known quirk: on the C6's native USB, ~1% of chunk *acks* drop at 4 KB
flash-erase boundaries (data is written correctly — only the reply notify is
lost while the single core erases). BLE transfers are unaffected.

## Wi-Fi pairing

Settings → **wifi setup** shows a QR code; scan it with a phone camera to
join the device's `Buddy-XXXX` AP, and the captive portal pops up to pick
your network and enter its password. Reconnects on boot once provisioned;
Settings → reset → **forget wifi** wipes it. Scriptable channel:
`{"cmd":"wifi","ssid":"…","pass":"…"}` (also `"portal":true` /
`"forget":true`) over USB or BLE.

## Cloud fleet (`cloud/`)

A Cloudflare Worker is the device hub *and* a fleet manager: device registry,
firmware storage (R2), broadcast OTA, and a dashboard. `pnpm run dev` in
`cloud/` runs it locally on the LAN; point a device at it with
`{"cmd":"hub","url":"http://<lan-ip>:8787","token":"dev-fleet-key"}` and it
self-registers. Upload a firmware version and broadcast to update the whole
fleet over the air. See [docs/cloud.md](docs/cloud.md).

## Hub over HTTP

`{"cmd":"hub","url":"http://host:8787"}` points the device at a hub API it
polls once a second (`GET /poll` for pending JSON lines, `POST /push` for
its replies) whenever Wi-Fi is up. Persisted; survives reboot. Local dev
hub: `python tools/test_hub.py`, then
`curl -d '{"cmd":"status"}' http://localhost:8787/queue` and watch the
device answer over the air.

## Roadmap

- M2: native-env tests for line-bus framing/dispatch and agent-state
- M3: cloud fleet hub — **done** (`cloud/`, [docs/cloud.md](docs/cloud.md));
  verified e2e over `wrangler dev`. Production deploy needs the device-side
  HTTPS change (WiFiClientSecure in transport-http + lib/ota)
- M4: e-paper dashboard app (XIAO 7.5") — second composition over the same legos
