# buddy-kit — agent rules

Lego-layered PlatformIO monorepo for ESP32 companion devices, extracted from
claude-desktop-buddy-esp32 (MIT). The buddy app (Claude desktop BLE/Wi-Fi
companion pet) is the first composition. Full breakdown lives in
[docs/](docs/README.md) — read `docs/architecture.md` before structural work.

## The rules (violations are bugs, not style)

1. **Dependencies point down.** `lib/*` never includes app headers. App
   policy enters libs via injected contracts — link-time functions
   (`facesSpeciesLoad/Save`, `wifiCreds*`, `agentStateRandom`) or a const
   function-pointer struct handed over once at init (`FileSink`); never
   `std::function`, never an app include.
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

## Docs & decisions contract — keep them true

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
| cloud Worker (fleet API, hub, dashboard, deploy) | `docs/cloud.md` |
| boards, partition table, NVS keys, USB/display quirks, memory | `docs/hardware.md` |
| anything that changes a recipe | `docs/extending.md` |
| repo layout / quickstart | `README.md` |
| **material decision** — new dep, layer contract, memory/partition layout, protocol shape, infra choice | ADR in `docs/decisions/`, same commit |

ADR lifecycle: `approved` at commit time; never edit an approved ADR —
supersede it (convention: `docs/decisions/README.md`).

If a session discovers a new field-verified gotcha (the kind that cost an
hour), it goes in the matching doc immediately.

## Build / verify loop

```bash
cd apps/buddy
pio run -e waveshare-esp32c6-touch-amoled-2-16 -t upload   # port+baud pinned in ini
pio device monitor                 # 115200
python3 tools/test_serial.py        # personas via USB
python3 tools/test_hub.py           # local hub for transport-http
```

Every refactor stage gets flashed and exercised on the C6 before commit —
build-green is not done. Two traps: opening the serial port DTR-resets the
chip (wait ~2 s before the first command), and UI changes need the physical
checklist in `docs/extending.md`.

## Skills

Reusable workflows live as skills; canonical source is
`.agents/skills/<name>/SKILL.md` (`.claude/skills/` holds symlinks for
Claude Code). Agents without native skill loading (Codex): before work
matching a description below, read that SKILL.md directly.

| Skill | Use when |
|---|---|
| `buddy-build-flash-verify` | building/flashing firmware, serial probing, panic triage, recovery |
| `buddy-hub-dev` | running the fleet hub locally, registering a device, debugging /poll + /push |
| `buddy-release-ota` | shipping a firmware release to the fleet over OTA, rollback |
| `adr` | a change involves a material decision → record it in `docs/decisions/` |

## Hard-won gotchas (details in docs/hardware.md)

- **Partition table is load-bearing**: dual-OTA (`ota_8mb.csv`); NVS must end
  at 0xe000 (PlatformIO writes boot_app0.bin there every upload). Changing
  the app slot layout breaks OTA; nvs/spiffs offsets must stay put so flashes
  preserve creds + characters. See docs/ota.md + docs/hardware.md.
  (ADR 005, ADR 006)
- LDF stays on `chain` (pinned in ini); `chain+`/`deep+` break LittleFS
  resolution on pioarduino. The C6 env `lib_ignore`s `BLE` (Bluedroid)
  deliberately. (ADR 004, ADR 008)
- `#define U8G2_FONT_SECTION(name)` must precede Arduino_GFX includes in
  any TU using U8g2 fonts.
- USB file push on the C6 drops ~1 % of chunk *acks* at flash-erase
  boundaries (data is written; BLE unaffected).

## Context

- Hardware ops (esptool recovery, backups, other boards): the sibling esp
  workspace `../esp/CLAUDE.md` and `../esp/boards/wsc6-amoled-2.16/`. The C6's
  factory restore is `../esp/boards/wsc6-amoled-2.16/board.py restore`.
- Upstream reference clone (read-only): `../esp/reference/claude-desktop-buddy-esp32/`.
- Roadmap: M3 cloud fleet hub DONE (`cloud/`, docs/cloud.md) — verified e2e
  over `wrangler dev`; prod deploy needs device-side HTTPS (WiFiClientSecure).
  Remaining: M2 native tests, M4 e-paper dashboard app, device HTTPS for prod.

## House style

Conventional commits, no AI attribution, comments explain constraints not
narration.
