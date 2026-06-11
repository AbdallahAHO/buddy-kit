# Architecture

buddy-kit is a monorepo of reusable firmware "legos" plus apps that compose
them. It was extracted from
[claude-desktop-buddy-esp32](https://github.com/vthinkxie/claude-desktop-buddy-esp32)
(MIT) — a BLE companion device for the Claude desktop apps — and restructured
so each layer can be swapped or reused (e.g. an e-paper dashboard fed by a
Cloudflare Worker instead of an AMOLED pet fed by BLE).

## The layers

```
┌──────────────────────────────────────────────────────────────┐
│ cloud/      — Cloudflare Worker: fleet hub + OTA (see cloud.md)│
│ apps/buddy — composition root (~470-line main.cpp)           │
│   app_state.h      the store: shared state, one header       │
│   screens/         organisms: one file per screen            │
│   overlays.cpp     menus as data tables                      │
│   input_router.cpp the focus ladder                          │
│   *_store.cpp      policy impls injected into libs           │
├──────────────────────────────────────────────────────────────┤
│ lib/ui-canvas      tokens + atoms + ListPanel molecule       │
│ lib/faces          ASCII species + GIF player (7-state)      │
│ lib/agent-state    AgentState schema + apply + selector      │  pure C++,
│ lib/line-bus       ByteSource pipe + framer + dispatch       │  native-
│ lib/file-push      chunked transfer protocol (FileSink)      │  testable →
│ lib/transport-usb / transport-ble / transport-http           │
│ lib/wifi-link      provisioning + connection lifecycle       │
├──────────────────────────────────────────────────────────────┤
│ hal/hw             display/input/power/imu/rtc/audio         │
│ hal/boards         one capability header per board           │
│ vendor/            upstream-vendored driver libs             │
└──────────────────────────────────────────────────────────────┘
```

**Dependency rule: everything points down, nothing points up.** Libs never
include app headers. When a lib needs app policy, the app injects it:

| Lib | Injected contract | App impl |
|---|---|---|
| file-push | `FileSink` (where files land, wipe rules, fit check) | `app_commands.cpp` |
| faces | `facesSpeciesLoad/Save` | `faces_store.cpp` |
| wifi-link | `wifiCredsLoad/Save/Clear` | `wifi_store.cpp` |
| agent-state | `agentStateRandom()` (entropy for matrixify) | `agent_link.cpp` |

Injection is **link-time** (plain functions the app must define), not
function pointers — a missing impl is a loud link error, and there's zero
runtime overhead.

## The React / atomic-design mapping

| Concept | Here |
|---|---|
| One-way data flow | `loop()` = pump → update store → route input → render. Render never writes shared state; input never paints. |
| Store | `app_state.h` (+ `tama`, the live agent snapshot) |
| Selectors | `agentDerive()` and the stats tier functions — pure |
| Tokens/atoms/molecules | `lib/ui-canvas`: colors + `Palette`, text/wrap/QR atoms, `uiListPanel` |
| Organisms | `apps/buddy/src/screens/*` — explicit canvas param, screen-local state (`useState`-style) behind tiny APIs |
| Synthetic events + capture | `input_router.cpp` — one priority ladder for buttons/touch/gestures |
| Config over code | `overlays.cpp` — menus are row tables driving one widget |

Deliberate non-React compromise: screen draw functions read the store
directly (hooks-style) instead of receiving prop structs — prop-drilling
would double the code for no behavior gain at this size. The hard contract
is: **render functions never mutate shared state.**

## Hard constraints (the C6 sets the bar)

- ESP32-C6: 512 KB RAM, **no PSRAM**, single core. No `std::function`,
  no `std::vector`, no heap in hot paths. Fixed-size buffers everywhere.
- Contracts are C-style: structs of function pointers (`ByteSource`,
  `FileSink`) or link-time symbols.
- `lib/agent-state` and `lib/line-bus` must stay Arduino-free (they compile
  under PlatformIO's `native` platform for tests).

## Packaging

PlatformIO private libs: each `lib/<name>` has a `library.json` declaring its
own registry deps; apps reach them via `lib_extra_dirs`. LDF runs in default
`chain` mode — the `chain+`/`deep+` modes break FS.h/LittleFS resolution on
the pioarduino platform. The two BLE stack impls coexist in transport-ble
behind `-DBUDDY_BLE_NIMBLE` / `-DBUDDY_BLE_BLUEDROID`; the C6 env also
`lib_ignore`s the core `BLE` lib because chain-mode LDF scans the
`#ifdef`'d-out Bluedroid impl.
