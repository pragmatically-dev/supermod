-- 007_notifications.sql — Supermod Planner notification center (schema v7).
-- Mantener sincronizado con kMigrationV7 en db/Database.cpp.
-- Historial persistente de alertas disparadas (event 30/15/5 min antes; task) con
-- estado leído + snooze. Aditivo (tabla nueva). Idempotente (no-op si user_version >= 7).

CREATE TABLE planner_notifications (
    id           TEXT PRIMARY KEY,          -- dedup key "<kind>:<ref_id>:<mins>"
    kind         TEXT    NOT NULL DEFAULT 'event',
    ref_id       TEXT    NOT NULL DEFAULT '',
    title        TEXT    NOT NULL DEFAULT '',
    ymd          TEXT    NOT NULL DEFAULT '',
    mins         INTEGER NOT NULL DEFAULT 0,
    fired_epoch  INTEGER NOT NULL DEFAULT (unixepoch()),
    read         INTEGER NOT NULL DEFAULT 0  CHECK (read IN (0,1)),
    snooze_epoch INTEGER,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX idx_planner_notif_fired  ON planner_notifications(fired_epoch DESC);
CREATE INDEX idx_planner_notif_unread ON planner_notifications(read, fired_epoch DESC);

PRAGMA user_version = 7;
