# buddy-kit — guidance for Claude

Lego-layered PlatformIO monorepo for ESP32 companion devices, extracted from
claude-desktop-buddy-esp32 (MIT). The buddy app (Claude desktop BLE/Wi-Fi
companion pet) is the first composition. Full breakdown lives in
[docs/](docs/README.md) — read `docs/architecture.md` before structural work.

## The rules (violations are bugs, not style)

1. **Dependencies point down.** `lib/*` never includes app headers. App
   policy enters libs via injected contracts (`FileSink`,
   `facesSpeciesLoad/Save`, `wifiCreds*`, `agentStateRandom`) — link-time
   functions, not function pointers.
2. **One-way flow.** Render functions never write shared state; input code
   never paints. Shared state goes through `app_state.h`; screen-local
   state stays inside its `screens/*.cpp`.
3. **The ghost rule.** Anything painting outside the buddy/HUD territories
   (overlays, full-screen surfaces) must full-clear on close
   (`applyDisplayMode()` + buddy invalidate). See `docs/rendering.md`.
4. **One place each:** input priority ladder → `input_router.cpp`; menus →
   row tables in `overlays.cpp`; command dispatch → `APP_CMDS[]` in
   `app_commands.cpp`. Never re-introduce parallel copies.
5. **Heapless contracts.** The C6 (512 KB, no PSRAM) is the floor: no
   std::function/std::vector, fixed-size buffers, no heap in hot paths.
   `lib/agent-state` and `lib/line-bus` stay Arduino-free (native tests).
6. **Acks fan out.** Replies always go through `gLineOut` (USB+BLE+hub),
   never `Serial` alone.

## Docs contract — keep them true

Docs live in `docs/` and are part of the change, not an afterthought.
**When you touch the left column, update the right column in the same
commit:**

| Code touched | Doc to update |
|---|---|
| layer boundaries, lib contracts, new lib/app | `docs/architecture.md` |
| loop order, render dispatch, clears/invalidates, display push, pacing | `docs/rendering.md` |
| transports, framing, dispatch, store/AgentState, stats persistence | `docs/data-flow.md` |
| input ladder, overlays tables, gestures | `docs/input.md` |
| any cmd/ack, snapshot fields, file push, hub routes | `docs/protocol.md` |
| wifi-link, portal/QR pairing, transport-http | `docs/connectivity.md` |
| OTA (lib/ota, dev_ota, partition table) | `docs/ota.md` |
| boards, partition table, NVS keys, USB/display quirks, memory | `docs/hardware.md` |
| anything that changes a recipe | `docs/extending.md` |
| repo layout / quickstart | `README.md` |

If a session discovers a new field-verified gotcha (the kind that cost an
hour), it goes in the matching doc immediately.

## Build / verify loop

```bash
cd apps/buddy
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload   # port+baud pinned in ini
python tools/test_serial.py        # personas via USB
python tools/test_hub.py           # local hub for transport-http
```

Every refactor stage gets flashed and exercised on the C6 before commit —
build-green is not done. Serial probes: send `{"cmd":"status"}\n` at 115200
(wait ~2 s after opening the port; DTR resets the chip). UI changes need the
physical checklist in `docs/extending.md`.

## Hard-won gotchas (details in docs/hardware.md)

- **Partition table is load-bearing**: dual-OTA (`ota_8mb.csv`); NVS must end
  at 0xe000 (PlatformIO writes boot_app0.bin there every upload). Changing
  the app slot layout breaks OTA; nvs/spiffs offsets must stay put so flashes
  preserve creds + characters. See docs/ota.md + docs/hardware.md.
- LDF stays on default `chain`; `chain+`/`deep+` break LittleFS resolution
  on pioarduino. The C6 env `lib_ignore`s `BLE` (Bluedroid) deliberately.
- `#define U8G2_FONT_SECTION(name)` must precede Arduino_GFX includes in
  any TU using U8g2 fonts.
- USB file push on the C6 drops ~1 % of chunk *acks* at flash-erase
  boundaries (data is written; BLE unaffected).

## Context

- Hardware ops (esptool recovery, backups, other boards): the parent
  workspace `../CLAUDE.md` and `../boards/wsc6-amoled-2.16/`. The C6's
  factory restore is `../boards/wsc6-amoled-2.16/board.py restore`.
- Upstream reference clone (read-only): `../reference/claude-desktop-buddy-esp32/`.
- Roadmap: M2 native tests → M3 Cloudflare Worker hub (speaks the two-route
  contract in `docs/protocol.md`) → M4 e-paper dashboard app.
- House style: conventional commits, no AI attribution, comments explain
  constraints not narration.
