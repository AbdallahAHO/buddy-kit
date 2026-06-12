# Extending

Recipes, ordered by frequency. Every recipe ends the same way: build, flash
to the C6, exercise it, and update the matching doc (see ../AGENTS.md).

## Add a settings / menu / reset row

One table row in `apps/buddy/src/overlays.cpp`:

```cpp
static void vMyThing(char* b, size_t c, uint16_t* col) { snprintf(b, c, " on"); *col = GREEN; }
static bool cMyThing(uint8_t) { /* do it */ return true; }  // true = stay open
// add to SETTINGS_ITEMS[] (the Overlay row count is computed):
{ "my thing", vMyThing, cMyThing, false },   // last field: arm-to-confirm
```

Selection, drawing, touch hit-testing and the "really?" flow come free.

## Add a screen (surface)

1. `apps/buddy/src/screens/myscreen.{h,cpp}` — `void myDraw(Arduino_GFX& g);`
   plus screen-local state behind tiny accessors. Read shared state via
   `app_state.h`; **never write it from the draw function**.
2. One line in main.cpp's surface chain (order = priority).
3. A rung in `input_router.cpp`'s ladder for its input.
4. **Its close path must full-clear** (`applyDisplayMode()` + buddy
   invalidate) if it paints outside the buddy/HUD territories — see
   rendering.md's ghost rule.

## Add a command

One handler + one row in `APP_CMDS[]` (`app_commands.cpp`). Ack through
`gLineOut` (`lineBusAck`) so the reply reaches USB, BLE and the hub. Update
protocol.md's table.

## Add a transport

Implement `ByteSource` (see `transport-usb` for the 20-line template, or
`transport-http` for the background-task + rings pattern when the medium
blocks). Then in the app: register with `gLineOut.add(...)` in
`appCommandsInit()` and pump it with its own `LineFramer` in
`agent_link.cpp::dataPoll`. Nothing else changes — commands, acks, snapshots
and file push ride any byte pipe.

## Add a face (ASCII species)

`lib/faces/src/buddies/<name>.cpp`: seven draw functions (sleep/idle/busy/
attention/celebrate/dizzy/heart) using the `buddy_common.h` helpers, export
`const Species <NAME>_SPECIES`, register it in `buddy.cpp`'s table. GIF
characters need no code — push a folder with per-state GIFs + manifest.json
(see protocol.md / `tools/prep_character.py`).

## Add a board

1. `hal/boards/src/boards/board_<name>.h` — pins + capability flags (copy
   the closest existing header; the flags are documented by example across
   the four boards).
2. A branch in `hal/hw/src/hw/pins.h`.
3. An `[env:<name>]` in `apps/buddy/platformio.ini` with `-DBOARD_<NAME>`
   and the right `-DBUDDY_BLE_*` stack flag.

## Add an app (the point of the kit)

`apps/<name>/` = a `platformio.ini` + a main.cpp composing the legos you
want + the policy stubs those legos require + your screens. Copy from the
closest app and trim (`apps/glance`, the hub-fed status panel, is this
recipe exercised for real):

1. `platformio.ini` — keep `lib_extra_dirs` (`../../lib`, `../../hal`,
   `../../vendor`) and chain-mode LDF; copy your board's `[env:…]` block
   (build flags, `lib_ignore`, pinned port + baud); trim `lib_deps` to
   your composition — chain mode means each app declares its own registry
   deps (glance drops AnimatedGIF with faces, and the NimBLE dep +
   `-DBUDDY_BLE_*` flag with BLE).
2. Copy `ota_8mb.csv` in next to it — `board_build.partitions` resolves
   app-locally, and the layout is load-bearing (hardware.md, ota.md).
3. Policy stubs only for the legos you chose (the injected-contracts
   table in architecture.md): wifi-link → `wifi_store.cpp`, faces →
   `faces_store.cpp`, file-push → a `FileSink`, agent-state →
   `agentStateRandom()`. A missing stub is a loud link error.
4. The app owns its own `LineOut gLineOut` and `APP_CMDS[]` dispatch —
   rules 4 and 6 apply per app.
5. Add the app's env(s) to the `firmware` matrix in
   `.github/workflows/ci.yml` (an `include:` entry per app × board) —
   otherwise the new app gets zero compile coverage and rots.

buddy is the full-featured reference composition; glance is the minimal
one — no BLE, no faces, no input routing, one screen. If its command
surface diverges from buddy's, note that in protocol.md's per-app section
(the docs contract row "any cmd/ack" applies per app).

## Blocks (copy-in compositions)

A finished composition — screen + menu row + ladder rungs + store bit —
can be packaged as a **block**: verbatim source under `blocks/<name>/`
plus the whole lines it owns in the central tables (ADR 012):

```bash
python3 tools/blocks.py add wifi-pairing      # copy files + patch tables
python3 tools/blocks.py rm wifi-pairing       # exact inverse (build stays green)
python3 tools/blocks.py verify wifi-pairing   # installed and undrifted?
python3 tools/blocks.py export wifi-pairing   # refresh block from the app
```

The loop: iterate on the screen *in the app* like any other code, then
`export` to update the block. Blocks patch whole lines only, which the
seam idiom guarantees: focus-ladder rungs are one line ending in `else`,
multi-line conditions keep one term per line, overlay row counts are
computed from the table. Keep new hook points in that shape and any
screen can be carved into a block later.

## Testing changes

| Layer | How |
|---|---|
| Pure libs (line-bus, agent-state) | PlatformIO `native` env tests (M2 skeleton in `test/`) |
| Protocol / state | `tools/test_serial.py`, `test_xfer.py`, `test_hub.py` over USB |
| UI / input | flash + the physical checklist: menus open/cycle/confirm, arm-confirm, page swipes, prompt approve/deny, no ghosts after closing overlays/QR/passkey |
