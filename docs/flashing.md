# Flashing from the browser

The kit installs over Web Serial with [ESP Web Tools](https://esphome.github.io/esp-web-tools/) —
no PlatformIO, no IDE. Rationale and the manifest-shape decision:
[ADR 013](decisions/013-browser-web-flasher.md).

## What the page does (`web/`)

A single static page (`web/index.html` + `flasher.js`), four steps:

1. **Choose a composition** — cards built from `web/apps.json` (Buddy, Glance),
   each with a real device screenshot.
2. **Flash** — ESP Web Tools writes the selected app. Leave its **erase**
   checkbox unchecked (the default): the multi-part manifest writes
   `bootloader`/`partitions`/`boot_app0`/`firmware` at their offsets and leaves
   NVS + spiffs alone, so **Wi-Fi creds and characters survive a flash** (like
   `pio upload`). Tick erase for a clean wipe.
3. **Connect & set Wi-Fi** — our own Web Serial client sends
   `{"cmd":"wifi",...}` and polls `{"cmd":"status"}`.
4. **See it live** — `{"cmd":"vdp","on":true}` streams the real framebuffer
   into a canvas (the [vdp tee](decisions/011-virtual-display-is-a-framebuffer-tee.md)).

Chrome or Edge on desktop only (Web Serial). If a board won't connect, put it
in download mode: hold **BOOT**, tap **RESET**, release **BOOT**.

## Run it locally

```bash
python3 tools/export_web_flasher.py            # build + assemble web/firmware/<app>/
cd web && python3 -m http.server 8123          # Web Serial works on localhost
# open http://localhost:8123 in Chrome → pick an app → Connect → flash
```

`export_web_flasher.py` runs `pio run` for each app, copies the four flash
parts into `web/firmware/<app>/`, and writes the ESP Web Tools `manifest.json`.
Add `--skip-build` to reuse existing `.pio` output, `--app buddy|glance` to
limit, `--version vX` to stamp the manifest. The C6 offsets it writes to
(`0x0 / 0x8000 / 0xe000 / 0x10000`) are load-bearing — see hardware.md.

## Publishing (when a remote exists)

Binaries are never committed (`web/firmware/` is gitignored). The dormant
workflows activate on first push:

- **`release.yml`** — tag `vX.Y.Z` → builds all apps → attaches
  `web-firmware.tar.gz` to the Release → deploys Pages with that firmware.
- **`pages.yml`** — redeploys on pushes to `main` that touch `web/`, and on
  manual dispatch, pulling the latest release's firmware
  (`fetch_release_firmware.py`). release.yml owns release deploys; a
  `GITHUB_TOKEN`-created release can't trigger `pages.yml`, so they never collide.

The flasher then lives at `https://<owner>.github.io/<repo>/`.
