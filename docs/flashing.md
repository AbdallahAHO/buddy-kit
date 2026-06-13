# Flashing from the browser

The kit installs over Web Serial with [ESP Web Tools](https://esphome.github.io/esp-web-tools/) —
no PlatformIO, no IDE. Rationale and the manifest-shape decision:
[ADR 013](decisions/013-browser-web-flasher.md).

## What the page does (`web/`)

A single static page (`web/index.html` + `flasher.js`), four steps:

1. **Choose a composition** — cards built from `web/apps.json` (Buddy, Glance),
   each with a real device screenshot.
2. **Flash** — ESP Web Tools writes the selected app. Multi-part manifest, so
   it writes only `bootloader`/`partitions`/`boot_app0`/`firmware` and leaves
   NVS + spiffs alone: **Wi-Fi creds and installed characters survive a flash**
   (same as `pio upload`).
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
limit, `--version vX` to stamp the manifest. The C6 offsets it merges around
(`0x0 / 0x8000 / 0xe000 / 0x10000`) are load-bearing — see hardware.md.

## Publishing (when a remote exists)

Binaries are never committed (`web/firmware/` is gitignored). The dormant
workflows activate on first push:

- **`release.yml`** — tag `vX.Y.Z` → builds all apps → uploads
  `web-firmware.tar.gz` to the GitHub Release → deploys Pages.
- **`pages.yml`** — pushes to `main` → pulls the latest release's firmware
  (`fetch_release_firmware.py`) → redeploys Pages.

The flasher then lives at `https://<owner>.github.io/<repo>/`.
