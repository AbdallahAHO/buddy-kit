// buddy-kit browser flasher.
// Step 2 (flashing) is handled by ESP Web Tools. Steps 1, 3, and 4 are here:
// the composition picker, plus a Web Serial client speaking the device's JSON
// line protocol — the same protocol lib/virtual-display and app_commands use.

const $ = (id) => document.getElementById(id);

// ── Browser gate ──────────────────────────────────────────────────────────
if (!("serial" in navigator)) {
  $("gate").innerHTML = "This page needs <b>Web Serial</b> — open it in desktop " +
    "<b>Chrome</b> or <b>Edge</b> (Safari and Firefox don't support it yet).";
  $("gate").style.display = "block";
} else if (!window.isSecureContext) {
  // file:// has navigator.serial but isn't a secure context — flashing won't work.
  $("gate").innerHTML = "Web Serial needs a secure context — serve this folder over " +
    "<b>http://localhost</b> (<code>python3 -m http.server</code>), not a <code>file://</code> path.";
  $("gate").style.display = "block";
}

// ── Step 1: composition picker ──────────────────────────────────────────────
const install = $("install");
let selected = null;

async function loadApps() {
  let catalog;
  try {
    catalog = await (await fetch("apps.json", { cache: "no-store" })).json();
  } catch {
    $("cards").innerHTML = '<p class="muted">Could not load apps.json.</p>';
    return;
  }
  const cards = $("cards");
  cards.innerHTML = "";
  for (const app of catalog.apps) {
    const el = document.createElement("button");
    el.className = "card";
    el.setAttribute("aria-selected", "false");
    el.innerHTML = `
      <img src="${app.screenshot}" alt="${app.name} screen" onerror="this.style.display='none'">
      <div class="name">${app.name}</div>
      <div class="tagline">${app.tagline}</div>
      <div class="blurb">${app.blurb}</div>`;
    el.onclick = () => selectApp(app, el);
    cards.appendChild(el);
  }
}

async function selectApp(app, el) {
  selected = app;
  document.querySelectorAll(".card").forEach((c) => c.setAttribute("aria-selected", "false"));
  el.setAttribute("aria-selected", "true");

  install.manifest = app.manifest; // ESP Web Tools resolves relative to the page
  $("sel2").textContent = "— " + app.name;
  $("step2").setAttribute("aria-disabled", "false");
  $("step3").setAttribute("aria-disabled", "false");

  // Tell the user plainly if firmware hasn't been built/published yet.
  const miss = $("fwmiss");
  miss.textContent = `Firmware for ${app.name} isn't built yet — run ` +
    `python3 tools/export_web_flasher.py --app ${app.id} (local), or it appears after a release.`;
  try {
    const ok = (await fetch(app.manifest, { method: "HEAD", cache: "no-store" })).ok;
    miss.style.display = ok ? "none" : "block";
  } catch {
    miss.style.display = "block";
  }
}

loadApps();

// ── Step 3/4: our own Web Serial client (JSON lines) ─────────────────────────
let port = null, writer = null, reader = null, rxBuf = "";
let lastWifiState = null, polling = false;
const cx = $("screen").getContext("2d");

const log = (s) => {
  const box = $("log");
  box.textContent += s + "\n";
  box.scrollTop = box.scrollHeight;
};

async function send(obj) {
  if (!writer) return;
  const line = typeof obj === "string" ? obj : JSON.stringify(obj);
  await writer.write(new TextEncoder().encode(line + "\n"));
}

// RGB565 little-endian stripe → canvas rows (matches lib/virtual-display).
function paintStripe(y, b64) {
  const raw = atob(b64), n = raw.length / 2, w = $("screen").width;
  const img = cx.createImageData(w, n / w);
  for (let i = 0; i < n; i++) {
    const c = raw.charCodeAt(2 * i) | (raw.charCodeAt(2 * i + 1) << 8);
    img.data[4 * i]     = (c >> 8) & 0xf8;
    img.data[4 * i + 1] = (c >> 3) & 0xfc;
    img.data[4 * i + 2] = (c << 3) & 0xf8;
    img.data[4 * i + 3] = 255;
  }
  cx.putImageData(img, 0, y);
}

function handle(line) {
  let m;
  try { m = JSON.parse(line); } catch { log(line); return; }
  if (m.vdp === "s") { paintStripe(m.y, m.b); return; }
  if (m.vdp === "info") {
    const cv = $("screen");
    cv.width = m.w; cv.height = m.h;
    cv.style.width = m.w * 1.5 + "px"; cv.style.height = m.h * 1.5 + "px";
    return;
  }
  if (m.ack === "status" && m.data) {
    const d = m.data, w = d.wifi || {};
    lastWifiState = w.state;
    const wifi = w.state === "online" ? `online @ ${w.ip}` : (w.state || "?");
    $("livestat").textContent = `${d.name || "device"} · wifi ${wifi} · heap ${Math.round((d.sys?.heap || 0) / 1024)} KB`;
    $("livestat").classList.remove("muted");
    return;
  }
  log(line);
}

async function readLoop() {
  try {
    while (port?.readable && reader) {
      const { value, done } = await reader.read();
      if (done) break;
      rxBuf += new TextDecoder().decode(value);
      let nl;
      while ((nl = rxBuf.indexOf("\n")) >= 0) {
        const line = rxBuf.slice(0, nl).trim();
        rxBuf = rxBuf.slice(nl + 1);
        if (line) handle(line);
      }
    }
  } catch (e) {
    if (port) log("read stopped: " + e.message);  // errors on unplug are expected
  } finally {
    try { reader?.releaseLock(); } catch {}
  }
}

// Tear down the serial connection and reset the UI to a reconnectable state.
async function cleanupSerial() {
  polling = false;
  try { await reader?.cancel(); } catch {}
  try { reader?.releaseLock(); } catch {}
  try { writer?.releaseLock(); } catch {}
  try { await port?.close(); } catch {}
  port = writer = reader = null; rxBuf = ""; lastWifiState = null;
  $("devpanel").style.display = "none";
  $("connect").disabled = false;
  $("connect").textContent = "Connect to device";
}

$("connect").onclick = async () => {
  if (port) return;                 // already connected — ignore repeat clicks
  $("connect").disabled = true;
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    // Register the unplug handler before the boot wait, so a yank mid-boot is caught.
    port.addEventListener("disconnect", () => {
      $("devstat").textContent = "device unplugged";
      cleanupSerial();
    });
    writer = port.writable.getWriter();
    reader = port.readable.getReader();
    readLoop();                     // releases the reader lock in its finally
    $("devstat").textContent = "booting (the port open reset the chip)…";
    await new Promise((r) => setTimeout(r, 2500)); // DTR reset → let it boot
    $("devstat").textContent = "connected";
    $("connect").textContent = "Connected";
    $("devpanel").style.display = "block";
    await send({ cmd: "vdp", on: true });   // start the live screen
    await send({ cmd: "status" });           // populate the status line
  } catch (e) {
    $("devstat").textContent = "connect failed: " + e.message;
    await cleanupSerial();
  }
};

$("wifi").onclick = async () => {
  const ssid = $("ssid").value.trim();
  if (!ssid) { $("devstat").textContent = "enter a Wi-Fi network name first"; return; }
  if (polling || !port) return;            // ignore re-clicks while a join is in flight
  polling = true;
  $("wifi").disabled = true;
  lastWifiState = null;
  await send({ cmd: "wifi", ssid, pass: $("pass").value });
  log(`→ joining ${ssid}…`);
  // Poll status until the live line flips to online (or give up after ~16 s).
  for (let i = 0; i < 8 && polling && port; i++) {
    await new Promise((r) => setTimeout(r, 2000));
    await send({ cmd: "status" });
    if (lastWifiState === "online") break;
  }
  polling = false;
  $("wifi").disabled = false;
};

$("send").onclick = async () => {
  const v = $("cmd").value.trim();
  if (v) { await send(v); log("→ " + v); }
};
$("cmd").addEventListener("keydown", (e) => { if (e.key === "Enter") $("send").onclick(); });
