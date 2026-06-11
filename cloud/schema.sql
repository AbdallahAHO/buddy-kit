-- Fleet registry + presence. One row per device; auto-created on first
-- authenticated poll, enriched from the device's identity headers.
CREATE TABLE IF NOT EXISTS devices (
  id          TEXT PRIMARY KEY,   -- device id (its BLE name, e.g. Claude-D41A)
  name        TEXT,
  model       TEXT,
  fw_version  TEXT,
  last_seen   INTEGER,            -- epoch ms of last poll
  last_ip     TEXT,
  last_status TEXT,               -- most recent status JSON the device pushed
  created_at  INTEGER
);

-- Firmware versions. The .bin lives in R2 under r2_key; this is the index.
CREATE TABLE IF NOT EXISTS firmware (
  version     TEXT PRIMARY KEY,
  r2_key      TEXT NOT NULL,
  size        INTEGER,
  notes       TEXT,
  uploaded_at INTEGER
);
