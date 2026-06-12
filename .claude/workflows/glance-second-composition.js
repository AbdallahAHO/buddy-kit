export const meta = {
  name: 'glance-second-composition',
  description: 'Scaffold apps/glance (hub-fed status panel), prove it on the C6 over USB, docs + adversarial review, restore buddy, land one atomic commit',
  whenToUse: 'Run when the C6 is plugged in over USB and you want the kit proven with a second app composition end-to-end (scaffold → build → flash → verify → review → commit).',
  phases: [
    { title: 'Scaffold', detail: 'write apps/glance sources from the frozen spec' },
    { title: 'Build', detail: 'pio compile-fix loop until green (no flashing)' },
    { title: 'Prove', detail: 'flash the C6, probe status, capture a vdp screenshot' },
    { title: 'Docs', detail: 'README/architecture/extending updates + recipe-drift fixes' },
    { title: 'Review', detail: 'parallel auditors: the 6 rules, docs contract, code review' },
    { title: 'Fix', detail: 'apply confirmed findings, rebuild green' },
    { title: 'Land', detail: 'restore buddy firmware, final probe, atomic commit' },
  ],
}

// ── Constants every agent needs ──────────────────────────────────────────
const KIT  = '/Users/abdallah/Developer/personal/buddy-kit'
const ENV  = 'waveshare-esp32c6-touch-amoled-2-16'
const PORT = '/dev/cu.usbmodem101'
const PY   = '$HOME/.local/share/uv/tools/platformio/bin/python' // pio is a uv tool; this python has pyserial

const GROUNDING = `
Repo: ${KIT} (work there; the session cwd may be elsewhere — use absolute paths).
Before writing anything, read in this order:
  1. ${KIT}/AGENTS.md                       — the 6 rules + docs contract (violations are bugs)
  2. ${KIT}/docs/architecture.md            — layers, injected contracts
  3. ${KIT}/docs/extending.md               — the "Add an app" recipe
  4. ${KIT}/.agents/skills/buddy-build-flash-verify/SKILL.md — build/probe protocol + TRAPS
Hard safety rules, no exceptions:
  - NEVER send {"cmd":"wifi","forget":true} — it wipes the user's Wi-Fi creds.
  - NEVER use shell echo/cat > ${PORT} loops — every port open DTR-resets the chip.
  - Only the phase that owns the serial port may touch ${PORT}; if your prompt
    doesn't explicitly grant the port, do not open it.
  - Match house style: 2-space indent, comments explain constraints not narration,
    no AI attribution anywhere.`

// ── The frozen v0 spec (the workflow's contract — agents implement, not design)
const SPEC = `
apps/glance v0 — "fleet glance": a hub-fed status panel. Second composition,
deliberately different from buddy: NO BLE, NO faces, NO file-push, NO hid-mouse.

Legos composed: line-bus, wifi-link, transport-http, agent-state, ui-canvas,
virtual-display, ota. HAL as-is.

Files to create under ${KIT}/apps/glance/:
  platformio.ini    Start from apps/buddy/platformio.ini: keep [env] section
                    (platform zip pin, lib_extra_dirs, lib_deps minus
                    "bitbank2/AnimatedGIF"), keep ota_8mb.csv + littlefs.
                    ONE env only: [env:${ENV}] copied from buddy's, but
                    WITHOUT -DBUDDY_BLE_NIMBLE and without the NimBLE lib_dep;
                    keep lib_ignore (BLE etc.) and port/baud pins. Set
                    -DBUDDY_FW_VERSION=\\"glance-0.1.0\\". Copy ota_8mb.csv +
                    no_ota_8mb.csv? — only ota_8mb.csv is referenced; copy it
                    from apps/buddy/ (board_build.partitions is app-relative).
  src/glance_state.h    Tiny store: extern TamaState tama; W/H constexpr from
                    hw/display.h (same pattern as buddy app_state.h, minus UI modes).
  src/wifi_store.cpp    Copy apps/buddy/src/wifi_store.cpp verbatim (the
                    injected wifiCredsLoad/Save/Clear contract → NVS "buddy"
                    namespace, so existing creds are reused — that reuse IS the proof).
  src/glance_link.cpp   agentStateRandom() → esp_random(); LineFramer<1024>
                    pumps for usbSerialSource() + httpByteSource(); _applyLine:
                    try glanceCommand(doc) first, else agentApplyJson into tama
                    (handle TIME_SYNC → hwRtcWrite like buddy's agent_link.cpp).
  src/glance_commands.cpp  LineOut gLineOut (usb + http) — acks fan out (rule 6).
                    LineOut vdpOut (usb only). APP_CMDS: status (slim ack:
                    name "glance", sys{up,heap}, wifi{state,ssid,ip},
                    hub{url,ok}, ota{slot}), wifi (join + portal — portal works
                    headless; NO screen flag), hub (same as buddy incl. NVS
                    persist + autostart in init), ota, vdp. Init: wire sinks,
                    virtualDisplayInit(hwCanvas()->getFramebuffer(), W, H, &vdpOut),
                    httpTransportSetIdentity(name, BOARD_MODEL_LINE1, BUDDY_FW_VERSION),
                    restore huburl/hubtok from NVS and httpTransportStart.
  src/screens/status.cpp  One full-screen panel, ui-canvas atoms only, repaint
                    every frame (full-screen owner → no ghost-rule close path
                    needed, note it in a comment). Rows: title "fleet glance";
                    clock HH:MM if RTC synced; agent line (sessions running/
                    waiting + tokens today, or "no agent data"); wifi: state +
                    ssid + ip; hub: url tail + ok/down; heap KB + fw version.
                    Use characterPalette()? NO — faces is excluded; define a
                    static const Palette in the screen (bg black, text white,
                    dim grey, body green-ish) and pass it around.
  src/main.cpp      (~100 lines) setup: Serial.setRxBufferSize(2048); hwInit();
                    glanceCommandsInit(); wifiLinkInit(); if creds → wifiLinkConnect();
                    otaInit(nullptr) — check lib/ota's init signature first and
                    adapt (progress callback may be nullptr-tolerant; if not,
                    pass a no-op). loop: wifiLinkTick(); pump (dataPoll-like);
                    render status screen into *hwCanvas(); hwDisplayPush();
                    virtualDisplayTick(); delay pacing ~50 ms.
Constraints (compiler-checked + reviewed): deps point down — never include
buddy headers; heapless — no std::function/vector, fixed buffers, no heap in
the loop; one-way flow — the screen reads state, never writes it.
NOT in scope v0: pairing QR screen (blocks/wifi-pairing can come later),
buttons/input routing, overlays, BLE anything.`

// ── Phase helpers ────────────────────────────────────────────────────────
const FILES_SCHEMA = { type: 'object', required: ['files', 'deviations'], properties: {
  files: { type: 'array', items: { type: 'string' } },
  deviations: { type: 'array', items: { type: 'string' }, description: 'anything done differently from the spec, with why' },
}}
const BUILD_SCHEMA = { type: 'object', required: ['green', 'attempts', 'ram', 'flash', 'notes'], properties: {
  green: { type: 'boolean' }, attempts: { type: 'number' },
  ram: { type: 'string' }, flash: { type: 'string' },
  notes: { type: 'array', items: { type: 'string' } },
}}
const PROVE_SCHEMA = { type: 'object', required: ['flashed', 'statusOk', 'wifiOnline', 'heap', 'stripes', 'pngPath'], properties: {
  flashed: { type: 'boolean' }, statusOk: { type: 'boolean' },
  wifiOnline: { type: 'boolean', description: 'did wifi reach online using NVS creds (the identity-survival proof)' },
  heap: { type: 'number' }, stripes: { type: 'number' }, pngPath: { type: 'string' },
}}
const FINDINGS_SCHEMA = { type: 'object', required: ['findings'], properties: {
  findings: { type: 'array', items: { type: 'object', required: ['title', 'file', 'severity', 'detail'], properties: {
    title: { type: 'string' }, file: { type: 'string' },
    severity: { enum: ['blocker', 'should-fix', 'nit'] }, detail: { type: 'string' },
  }}},
}}

// ═════════════════════════════════════════════════════════════════════════
phase('Scaffold')
log('Writing apps/glance from the frozen spec')
const scaffold = await agent(`${GROUNDING}

Create the new app exactly per this spec — implement, don't redesign:
${SPEC}

Use apps/buddy/src/* as the style reference: read app_commands.cpp,
agent_link.cpp, wifi_store.cpp, main.cpp before writing your versions.
Check every lib header you call (lib/*/src/*.h) for the real signatures —
do not guess APIs. Do NOT run pio (the next phase builds). Do NOT touch
the serial port. Do NOT modify anything outside apps/glance/.
Return the file list and any spec deviations you had to make.`,
  { label: 'scaffold:glance', schema: FILES_SCHEMA })
if (!scaffold) return { failed: 'scaffold agent died' }
log(`scaffolded ${scaffold.files.length} files; deviations: ${scaffold.deviations.length}`)

// ═════════════════════════════════════════════════════════════════════════
phase('Build')
const build = await agent(`${GROUNDING}

Make apps/glance compile green:
  cd ${KIT} && pio run -d apps/glance -e ${ENV}
Iterate on compile/link errors (max 8 attempts). Link errors naming missing
symbols are the injected-contract checklist working — define the missing stub
in the right glance file (see docs/architecture.md's contract table), never
by including buddy headers and never by editing lib/ or hal/. If a lib
genuinely needs a change, STOP and report it as a note instead.
Do NOT use -t upload. Do NOT touch the serial port.
Report final RAM/Flash usage lines from the successful build.`,
  { label: 'build:glance', schema: BUILD_SCHEMA })
if (!build || !build.green) return { failed: 'build never went green', build }
log(`build green in ${build.attempts} attempt(s) — RAM ${build.ram}, Flash ${build.flash}`)

// ═════════════════════════════════════════════════════════════════════════
phase('Prove')
const prove = await agent(`${GROUNDING}

You own the serial port ${PORT} for this phase. The C6 on the desk currently
runs the buddy app; its Wi-Fi creds + hub URL live in NVS and MUST survive.

1. Flash:  cd ${KIT} && pio run -d apps/glance -e ${ENV} -t upload
2. Probe with pyserial via:  ${PY}
   Open ${PORT} at 115200, sleep 2.5 s (DTR reset), then send
   {"cmd":"status"}\\n and parse the ack. Gates:
     - statusOk: ack ok with name "glance"
     - wifiOnline: poll status every 5 s up to 90 s until wifi.state == "online"
       (creds come from NVS — this is the identity-survival proof). "joining"
       that never lands is a FAIL; report the observed state.
     - heap: record sys.heap (expect well above buddy's ~95 KB — no BLE stack).
3. Capture the screen over the vdp protocol: send {"cmd":"vdp","on":true},
   collect the info line + all stripes ({"vdp":"s","y":...,"b":base64}) until
   h/sh stripes arrived (~10 s timeout), send {"cmd":"vdp","on":false}.
   Reassemble RGB565-LE → PPM → /tmp/glance-screen.ppm, then
   sips -s format png --resampleWidth 368 /tmp/glance-screen.ppm --out /tmp/glance-screen.png
   (use apps/buddy's capture pattern; see docs/protocol.md "Virtual display").
4. Close the port cleanly. Do not send wifi or hub commands of any kind.
Report all gate results honestly — a failed gate with accurate detail is a
good outcome, a glossed-over gate is not.`,
  { label: 'prove:c6', schema: PROVE_SCHEMA })
if (!prove || !prove.flashed) return { failed: 'flash failed', prove }
log(`proof: status=${prove.statusOk} wifi=${prove.wifiOnline} heap=${prove.heap} stripes=${prove.stripes} → ${prove.pngPath}`)

// ═════════════════════════════════════════════════════════════════════════
phase('Docs')
const docs = await agent(`${GROUNDING}

apps/glance now exists, builds, and ran on hardware. Honor the docs contract
(AGENTS.md table) for this change — same-commit docs, so edit them now (the
Land phase commits everything together):
  - README.md: repo layout/quickstart — mention apps/glance as the second
    composition (one or two lines, match existing tone).
  - docs/architecture.md: the layers diagram lists "apps/buddy — composition
    root"; reflect that apps/ now has two compositions (minimal edit).
  - docs/extending.md "Add an app": this recipe was just exercised for real
    for the first time. Compare it against what the Scaffold/Build phases
    actually needed (git status + read apps/glance/) and fix any drift —
    e.g. policy stubs the recipe doesn't mention, the partition-table copy,
    the lib_deps trim. Keep it a recipe, not a tutorial.
Do not write an ADR (composing existing legos is not a material decision).
Do NOT touch the serial port. Return findings as docs-drift notes.`,
  { label: 'docs:contract', schema: FINDINGS_SCHEMA })
log(`docs updated; drift notes: ${docs ? docs.findings.length : 0}`)

// ═════════════════════════════════════════════════════════════════════════
phase('Review')
const REVIEWERS = [
  { key: 'rules', prompt: `Audit ${KIT}/apps/glance against AGENTS.md's six rules, adversarially. Check: no buddy includes anywhere (grep); libs untouched (git status must show only apps/glance + docs + .claude/workflows); no std::function/std::vector/new/malloc/String in the loop path; acks go through gLineOut (usb+http), never Serial.print for protocol replies; the status screen never writes shared state. Cite file:line for every finding.` },
  { key: 'docs', prompt: `Audit the docs contract for the pending change in ${KIT} (git diff + new files): does every touched behavior have its doc row updated (AGENTS.md table)? Is extending.md's "Add an app" recipe now accurate against what apps/glance actually contains — could an agent follow it cold and get this result? Flag stale claims, missing rows, broken relative links in changed files.` },
  { key: 'code', prompt: `Code-review ${KIT}/apps/glance for real bugs: ArduinoJson misuse, buffer sizes vs what's written into them, missing null/length guards on NVS strings, LineFramer pump correctness vs apps/buddy/src/agent_link.cpp, init-order hazards (hwCanvas before virtualDisplayInit, wifiLinkInit before connect), loop pacing that could starve transport-http. Only report findings you are confident in.` },
]
const reviews = await parallel(REVIEWERS.map(r => () =>
  agent(`${GROUNDING}\n\n${r.prompt}\nDo NOT touch the serial port. Do NOT edit files — report only.`,
    { label: `review:${r.key}`, schema: FINDINGS_SCHEMA })))
const findings = reviews.filter(Boolean).flatMap(r => r.findings)
  .filter(f => f.severity !== 'nit')
log(`review: ${findings.length} blocker/should-fix finding(s)`)

// ═════════════════════════════════════════════════════════════════════════
phase('Fix')
let fixed = { applied: 0 }
if (findings.length) {
  const fix = await agent(`${GROUNDING}

Apply these review findings to ${KIT} (only the real ones — if a finding is
wrong, skip it and say why):
${JSON.stringify(findings, null, 2)}

After edits: pio run -d apps/glance -e ${ENV} must be green. If any fix
changes runtime behavior (not comments/docs), say so explicitly in your
report — the Land phase will re-flash. Do NOT touch the serial port.`,
    { label: 'fix:findings', schema: { type: 'object', required: ['applied', 'skipped', 'behaviorChanged'], properties: {
      applied: { type: 'number' }, skipped: { type: 'array', items: { type: 'string' } },
      behaviorChanged: { type: 'boolean' } } } })
  fixed = fix || { applied: 0, behaviorChanged: false }
  log(`fixes applied: ${fixed.applied}, behavior changed: ${!!fixed.behaviorChanged}`)
} else {
  log('no findings to fix — skipping')
}

// ═════════════════════════════════════════════════════════════════════════
phase('Land')
const keepGlance = !!(args && args.keepGlance)
const land = await agent(`${GROUNDING}

You own the serial port ${PORT} for this phase. Finish the job:

1. ${fixed.behaviorChanged
      ? `Fixes changed behavior — re-flash and re-gate glance first:
   pio run -d apps/glance -e ${ENV} -t upload, then re-probe status
   (2.5 s DTR wait) and confirm the ack still reads name "glance".`
      : 'No behavior-changing fixes — glance is already proven on the device.'}
2. ${keepGlance
      ? 'Leave glance running on the board (user chose keepGlance).'
      : `Restore the daily driver:  cd ${KIT} && pio run -d apps/buddy -e ${ENV} -t upload
   Then probe status (2.5 s DTR wait) and confirm the buddy is back
   (ack has name/owner/stats fields and wifi rejoins or is joining).`}
3. Commit ONE atomic conventional commit from ${KIT} containing apps/glance,
   the docs edits, and .claude/workflows/glance-second-composition.js:
   subject: "feat: apps/glance — second composition proves the kit"
   Body: 2-4 lines — what it composes, what it deliberately omits (BLE/faces/
   file-push), hardware proof summary (wifi from NVS creds, heap, vdp capture).
   NO AI attribution. Do NOT push.
4. git status must be clean afterwards.
Report the commit hash and the final device state.`,
  { label: 'land:commit', schema: { type: 'object', required: ['commit', 'deviceState', 'clean'], properties: {
    commit: { type: 'string' }, deviceState: { type: 'string' }, clean: { type: 'boolean' } } } })

return {
  scaffold: scaffold.files,
  build: { attempts: build.attempts, ram: build.ram, flash: build.flash },
  proof: prove,
  reviewFindings: findings,
  fixes: fixed,
  landed: land,
  screenshot: prove.pngPath,
}
