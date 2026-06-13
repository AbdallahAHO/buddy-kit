import { Hono } from "hono";
import type { D1Database, R2Bucket, DurableObjectNamespace } from "@cloudflare/workers-types";
import { DeviceHub } from "./hub";
import { dashboardHtml } from "./dashboard";

export { DeviceHub };

type Env = {
  HUB: DurableObjectNamespace;
  DB: D1Database;
  FW: R2Bucket;
  DEVICE_KEY: string;
  ADMIN_KEY: string;
  PUBLIC_URL?: string;
};

const app = new Hono<{ Bindings: Env }>();

// --- helpers --------------------------------------------------------------

const bearer = (c: any): string | null => {
  const h = c.req.header("Authorization") ?? "";
  return h.startsWith("Bearer ") ? h.slice(7) : null;
};
const hub = (env: Env, id: string) => env.HUB.get(env.HUB.idFromName(id));

// Device auth: the shared fleet key. (Per-device token issuance is a future
// hardening step — see docs/cloud.md.)
const deviceAuth = async (c: any, next: any) => {
  if (bearer(c) !== c.env.DEVICE_KEY) return c.text("unauthorized", 401);
  await next();
};
const adminAuth = async (c: any, next: any) => {
  // Admin key from the Authorization header only — never a query string
  // (?k= would leak the admin key into logs, history, and referrers).
  if (bearer(c) !== c.env.ADMIN_KEY) return c.text("unauthorized", 401);
  await next();
};

// Auto-register on first contact + refresh presence from identity headers.
async function touchDevice(env: Env, c: any) {
  const id = c.req.header("X-Device-Id");
  if (!id) return null;
  const now = Date.now();
  const ip =
    c.req.header("CF-Connecting-IP") ??
    c.req.header("X-Forwarded-For") ??
    "";
  await env.DB.prepare(
    `INSERT INTO devices (id, name, model, fw_version, last_seen, last_ip, created_at)
     VALUES (?1, ?1, ?2, ?3, ?4, ?5, ?4)
     ON CONFLICT(id) DO UPDATE SET
       model=COALESCE(excluded.model, model),
       fw_version=COALESCE(excluded.fw_version, fw_version),
       last_seen=excluded.last_seen,
       last_ip=excluded.last_ip`
  )
    .bind(id, c.req.header("X-Model") ?? null, c.req.header("X-Fw") ?? null, now, ip)
    .run();
  return id;
}

// --- device-facing (the hub contract) ------------------------------------

app.get("/poll", deviceAuth, async (c) => {
  const id = await touchDevice(c.env, c);
  if (!id) return c.text("missing X-Device-Id", 400);
  const r = await hub(c.env, id).fetch("https://do/drain");
  const body = await r.text();
  if (!body) return c.body(null, 204);
  return new Response(body, { headers: { "content-type": "application/x-ndjson" } });
});

app.post("/push", deviceAuth, async (c) => {
  const id = await touchDevice(c.env, c);
  if (!id) return c.text("missing X-Device-Id", 400);
  // Stash the most recent status line so the dashboard can show rich state.
  const body = await c.req.text();
  const lastStatus = body
    .split("\n")
    .reverse()
    .find((l) => l.includes('"ack":"status"'));
  if (lastStatus) {
    await c.env.DB.prepare(`UPDATE devices SET last_status=?2 WHERE id=?1`)
      .bind(id, lastStatus)
      .run();
  }
  return c.text("ok");
});

// Firmware download for OTA. Auth via ?t= so the device's plain-GET OTA
// pull (HTTPUpdate) needs no header support.
app.get("/fw/:version", async (c) => {
  if (c.req.query("t") !== c.env.DEVICE_KEY) return c.text("unauthorized", 401);
  const row = await c.env.DB.prepare(`SELECT r2_key FROM firmware WHERE version=?1`)
    .bind(c.req.param("version"))
    .first<{ r2_key: string }>();
  if (!row) return c.text("no such version", 404);
  const obj = await c.env.FW.get(row.r2_key);
  if (!obj) return c.text("blob missing", 404);
  return new Response(obj.body as any, {
    headers: { "content-type": "application/octet-stream" },
  });
});

// --- admin / fleet API ----------------------------------------------------

app.get("/v1/devices", adminAuth, async (c) => {
  const { results } = await c.env.DB.prepare(
    `SELECT id, name, model, fw_version, last_seen, last_ip, last_status
     FROM devices ORDER BY last_seen DESC`
  ).all();
  return c.json({ devices: results });
});

// Upload a firmware version: POST /v1/firmware?version=0.3.0  body = raw .bin
app.post("/v1/firmware", adminAuth, async (c) => {
  const version = c.req.query("version");
  if (!version) return c.text("missing ?version", 400);
  const key = `fw/${version}.bin`;
  const buf = await c.req.arrayBuffer();
  await c.env.FW.put(key, buf);
  await c.env.DB.prepare(
    `INSERT INTO firmware (version, r2_key, size, notes, uploaded_at)
     VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT(version) DO UPDATE SET r2_key=excluded.r2_key, size=excluded.size, uploaded_at=excluded.uploaded_at`
  )
    .bind(version, key, buf.byteLength, c.req.query("notes") ?? null, Date.now())
    .run();
  return c.json({ ok: true, version, size: buf.byteLength });
});

app.get("/v1/firmware", adminAuth, async (c) => {
  const { results } = await c.env.DB.prepare(
    `SELECT version, size, notes, uploaded_at FROM firmware ORDER BY uploaded_at DESC`
  ).all();
  return c.json({ firmware: results });
});

// Broadcast an OTA command to every registered device.
app.post("/v1/ota/broadcast", adminAuth, async (c) => {
  const { version } = await c.req.json<{ version: string }>();
  if (!version) return c.text("missing version", 400);
  const fw = await c.env.DB.prepare(`SELECT 1 FROM firmware WHERE version=?1`).bind(version).first();
  if (!fw) return c.text("no such firmware version", 404);

  // Prefer an explicit public URL so OTA links are reachable by devices even
  // when the admin call came from localhost (dev) or a different origin.
  const origin = c.env.PUBLIC_URL || new URL(c.req.url).origin;
  const url = `${origin}/fw/${version}?t=${c.env.DEVICE_KEY}`;
  const cmd = JSON.stringify({ cmd: "ota", url });

  const { results } = await c.env.DB.prepare(`SELECT id FROM devices`).all<{ id: string }>();
  await Promise.all(
    results.map((d) =>
      hub(c.env, d.id).fetch("https://do/enqueue", { method: "POST", body: cmd })
    )
  );
  return c.json({ ok: true, queued: results.length, version });
});

// Send an arbitrary command to one device.
app.post("/v1/devices/:id/cmd", adminAuth, async (c) => {
  const id = c.req.param("id");
  const cmd = await c.req.text(); // raw JSON line
  await hub(c.env, id).fetch("https://do/enqueue", { method: "POST", body: cmd });
  return c.json({ ok: true, id });
});

// --- dashboard ------------------------------------------------------------

app.get("/", (c) => c.html(dashboardHtml()));

app.get("/health", (c) => c.text("ok"));

export default app;
