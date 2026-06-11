# Rendering

There is no virtual DOM: everything paints onto **one shared canvas**
(184×224 RGB565, `hwCanvas()`, ~80 KB) and `hwDisplayPush()` ships it to the
panel once per frame. The logical canvas size is fixed across all boards —
board headers describe how it maps to their physical panel.

## The frame loop (`apps/buddy/src/main.cpp::loop`)

```
loop()
 ├─ hwInputUpdate()                 sample buttons + touch
 ├─ wifiLinkTick()                  Wi-Fi state machine (+ portal servers)
 │   ├─ auto-enable settings.wifi on first successful join
 │   └─ auto-close the QR screen 5 s after going online
 ├─ dataPoll(&tama)                 pump USB/BLE/HTTP into the store
 ├─ statsPollLevelUp() → celebrate one-shot
 ├─ baseState = agentDerive(tama)   pure selector
 ├─ activeState = one-shot ? animation : baseState
 ├─ hwBorderAlert(...)              attention border flash
 ├─ shake check → dizzy one-shot
 ├─ prompt arrival (promptId changed):
 │     wake + beep + force DISP_NORMAL + overlayClose + full clear
 ├─ inputRoute(now, inPrompt)       the focus ladder (see input.md)
 ├─ homeClockTick() + clocking transitions (full clear on enter/exit)
 ├─ passkey transitions (full clear on dismissal)
 ├─ RENDER (below) + hwDisplayPush()
 ├─ face-down nap logic (IMU)
 └─ frame pacing
```

## The render tree — three layers, painted bottom-up

```
┌─ LAYER 3: Overlay (topmost, optional) ───────────────────────────┐
│  overlayDraw(spr) → uiListPanel(rows, sel, border, hints)        │
│  rows come from data tables: MENU / SETTINGS / RESET overlays    │
├─ LAYER 2: Surface (exactly one wins) ────────────────────────────┤
│  blePasskey()   → passkeyDraw      full screen                   │
│  wifiSetupOpen  → wifiSetupDraw    full screen (QR + status)     │
│  clocking       → homeClockDraw    bottom band (pet stays above) │
│  DISP_INFO      → infoDraw         6 pages                       │
│  DISP_PET       → petDraw          2 pages                       │
│  settings.hud   → homeHudDraw      bottom strip (+ approval card │
│                                    while a prompt is pending)    │
├─ LAYER 1: Base ──────────────────────────────────────────────────┤
│  buddyMode        → buddyTick(activeState)    18 ASCII species   │
│  characterLoaded  → characterTick()           GIF per state      │
│  else             → install progress / "no character loaded"     │
└──────────────────────────────────────────────────────────────────┘
```

`clocking` = DISP_NORMAL + no overlay + no prompt + nothing running + RTC
synced. The persona has 7 states (`sleep idle busy attention celebrate dizzy
heart`); every face implements all 7.

## Pixel ownership — the ghost rule

Nothing erases automatically. Standing painters own territories:

```
 y=0   ┌────────────────────┐
       │  buddy / GIF       │ repaints on animation/invalidate
 y≈126 ├────────────────────┤
       │  no-man's band     │ ← only full-screen surfaces and the centered
 y≈146 ├────────────────────┤   overlay panel ever paint here
       │  HUD strip         │ repaints every frame
 y=224 └────────────────────┘
```

**The rule: anything that painted outside the standing territories must
trigger a full-canvas clear when it exits.** `applyDisplayMode()` is that
clear (fillScreen + peek update + character invalidate). It runs on:

- display-mode switches (Home ↔ Pet ↔ Info)
- prompt arrival (forces the approval screen)
- `overlayClose()` — menu/settings/reset panels
- `wifiSetupClose()` — the QR screen
- passkey dismissal (`blePasskey()` → 0 transition in main.cpp)
- clock-mode exit

If you add a surface that paints the middle band or the full canvas, wire
its close path the same way or it WILL leave ghosts (in ASCII-buddy mode
`characterInvalidate()` alone clears nothing).

## Display push (hal/hw/display.cpp)

Canvas → panel strategy is per-board via capability flags:

| Strategy | Boards | Notes |
|---|---|---|
| Streamed 2× | C6 2.16" | one continuous QSPI transaction, CS held — this panel blanks if CS toggles between rows |
| Letterbox bilinear | 1.75C, S3 2.16" | PSRAM full-frame buffer |
| Plain 2× per row | S3 1.8" | 240 MHz S3 outruns the panel timeout |

## Frame pacing

| Condition | Loop delay |
|---|---|
| Screen off | 200 ms |
| Interaction / prompt / overlay / transfer / passkey / wifi screen / one-shot | 16 ms (~60 fps) |
| Idle | 100 ms, sliced with touch-IRQ checks |

AMOLED care: face-down (IMU) naps the pet and dims; idle timeouts turn the
screen off (30 s battery, 5 min battery+clock, never on USB); a periodic
full redraw mitigates burn-in.
