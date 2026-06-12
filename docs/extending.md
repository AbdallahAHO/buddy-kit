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

`apps/<name>/` = `platformio.ini` (lib_extra_dirs at `../../lib`, `../../hal`,
`../../vendor`) + a main.cpp composing the legos you want + the policy stubs
the chosen libs require (`faces_store`, `wifi_store`, a `FileSink`,
`agentStateRandom`) + your screens. The buddy app is the reference
composition; an e-paper dashboard would reuse transports + agent-state +
wifi-link + ui-canvas wholesale and replace faces + screens + the push
strategy.

## Testing changes

| Layer | How |
|---|---|
| Pure libs (line-bus, agent-state) | PlatformIO `native` env tests (M2 skeleton in `test/`) |
| Protocol / state | `tools/test_serial.py`, `test_xfer.py`, `test_hub.py` over USB |
| UI / input | flash + the physical checklist: menus open/cycle/confirm, arm-confirm, page swipes, prompt approve/deny, no ghosts after closing overlays/QR/passkey |
