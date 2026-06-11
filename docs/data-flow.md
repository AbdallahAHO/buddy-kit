# Data flow

One parser, three transports, one store.

```
 USB serial ──┐
 BLE (NUS) ───┼── ByteSource ─→ LineFramer ─→ one JSON line ─→ _applyLine()
 HTTP hub ────┘   (byte pipe)    (\n splits,                       │
                                  1024 cap)        ┌───────────────┴────────────┐
                                             has "cmd"?                    snapshot
                                                   │                           │
                                            appCommand()               agentApplyJson()
                                            (app_commands.cpp)         (lib/agent-state)
                                              │        │                       │
                                       dispatch     filePushHandle()     mutates tama +
                                       table        (lib/file-push)      returns AgentEvent
                                              │                                │
                                       acks → LineOut ──────────┐   TIME_SYNC → RTC write
                                       (fans out to all 3 pipes)│   hasTokens → statsOnBridgeTokens
```

## The contracts (lib/line-bus)

- `ByteSource` — `available / read / write` function-pointer struct. USB,
  BLE and HTTP all satisfy it; `LineOut` fans replies out to every
  registered source (writes to a clientless pipe just drop).
- `LineFramer<N>` — accumulates bytes until `\n`/`\r`; lines must start with
  `{` to be applied (framing noise is discarded).
- `CmdEntry` table + `lineBusDispatch` — fixed-size command dispatch, no heap.

## Command routing quirk (preserved from upstream)

In `appCommand()`: registry table first, then `filePushHandle`. If no
transfer is active, **unknown cmds are swallowed — except `"permission"`**,
which falls through so the desktop's permission echo never parses as state.

## The store

- `tama` (`AgentState`): sessions total/running/waiting, recentlyCompleted,
  msg, transcript lines (8×92), prompt {id,tool,hint}, tokensToday.
  Non-ASCII bytes in `msg`/`promptTool`/`promptHint` are "matrixified" into
  random rain glyphs (ASCII-only font); `promptId` is never touched (echoed
  verbatim) and `entries` keep raw UTF-8 (CJK font renders them).
- `app_state.h`: UI shared state (displayMode, persona, flags, geometry).
- Screen-local state stays inside its screen file.

## Liveness & modes (agent_link.cpp)

Priority: **demo** (menu toggle, fake scenarios every 8 s) → **live** (any
valid JSON in the last 30 s) → **asleep** ("No Claude connected", zeros).
BLE counts as active if bytes arrived within 15 s (desktop keepalive is
~10 s).

## Events out of agent-state

- `TIME_SYNC {epoch, tz}` → app writes the RTC, marks it valid (gates the
  clock face).
- `hasTokens` → `statsOnBridgeTokens` — cumulative bridge total, delta-
  synced with a **first-sight latch** (device reboot must not re-credit the
  bridge's whole session; bridge restart = total drops = resync silently).

## Stats persistence (stats.cpp, NVS namespace "buddy")

Saves happen on significant events only (approval, denial, nap end,
level-up) — never on a timer; tokens accumulate in RAM and persist on level
milestones (50 K/level). NVS sectors are ~100 K writes; treat them as a
budget. All names that go into the printf'd status JSON are sanitized of
quotes/backslashes on the way in.
