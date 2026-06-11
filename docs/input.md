# Input

All raw input (two buttons + the AXP power key + capacitive touch) is
handled in **one** place: `apps/buddy/src/input_router.cpp::inputRoute()`.
Render code never handles input; input code never paints.

## The focus ladder

Whoever is topmost captures the event — checked in this order everywhere:

```
            button A / B / touch
                    │
 1. wifi QR screen open?   any key or tap → wifiSetupClose()
 2. permission prompt?     A or top-half tap   → APPROVE (+ ♥ if < 5 s)
                           B or bottom-half tap → DENY
 3. overlay open?          A: next row    B or row-tap: confirm
                           hold-A: close
 4. nothing focused        A: cycle Home → Pet → Info
                           B: next page / scroll transcript
                           swipe ↕: 9-page carousel (Home, Pet 1-2, Info 1-6)
                           swipe ↔ (clock mode): cycle species
                           tap buddy: heart   ·  shake: dizzy
```

Other physical inputs: AXP key short-tap = screen off/on toggle; 6 s hold =
hardware power-off; any button wakes a sleeping screen and that press is
**swallowed** (it won't also act).

Permission decisions are sent from the router via `sendCmd` →
`gLineOut`, so they reach USB, BLE **and** the HTTP hub.

## Overlays as data (`overlays.cpp`)

Menu, settings and reset are **row tables** driving one props-based widget
(`uiListPanel` in lib/ui-canvas). An item is:

```cpp
{ "label",  valueFn /*or nullptr*/,  confirmFn,  armToConfirm }
```

- `valueFn(buf, cap, &color)` renders the right-hand column (on/off, "3/4",
  wifi state…).
- `confirmFn(idx)` returns `true` to keep the overlay open (toggles) or
  `false` to close it (navigation / one-shot actions).
- `armToConfirm` gives destructive rows the tap-twice flow: first confirm
  flips the label to "really?" for 3 s; scrolling away disarms. Used by
  delete-char / forget-wifi / factory-reset.

Selection, the open-overlay pointer, arm state, and touch row hit-testing
(geometry mirrored from `uiListPanel*` helpers) all live in overlays.cpp —
adding a row touches **one table**, nothing else.

## Adding interaction — where things go

| You want | Touch |
|---|---|
| New settings/menu row | one row in the table in `overlays.cpp` |
| New full-screen surface | screen file + a rung in the ladder (close path!) + render dispatch line — see extending.md |
| New gesture | the release-classifier block in `inputRoute` |
