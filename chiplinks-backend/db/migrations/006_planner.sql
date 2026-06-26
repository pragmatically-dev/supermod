-- Schema v6 — Supermod Planner (perpetual calendar/planner app).
--
-- Source-of-truth para la migración v5→v6. El string embebido vive en
-- Database.cpp (kMigrationV6); mantener ambos sincronizados.
--
-- App planner nativa (BLoC) dentro de supermod: calendario PERPETUO (los datos
-- viven por fecha real, no por "año fijo" del PDF), con eventos, tareas/TODOs,
-- hábitos, y la tinta nativa de cada página enganchada a un notebook "scratch"
-- oculto (mismo patrón PUNTERO que los post-its: ver 002_postits.sql). Las
-- "extras" estructuradas (goals/budget/colecciones/reviews) usan una tabla
-- genérica `planner_entries` (kind+bucket) que las fases posteriores tipan.
-- Todo soft-delete + created/updated_at + CHECK server-side. Sin doc_id (global).

-- ── Eventos de agenda (por día) ──────────────────────────────────────────────
CREATE TABLE planner_events (
    id          TEXT PRIMARY KEY,
    ymd         TEXT    NOT NULL,                          -- 'YYYY-MM-DD' (fecha local)
    start_min   INTEGER NOT NULL DEFAULT 0   CHECK (start_min >= 0 AND start_min <  1440),
    end_min     INTEGER NOT NULL DEFAULT 0   CHECK (end_min   >= 0 AND end_min   <= 1440),
    all_day     INTEGER NOT NULL DEFAULT 0   CHECK (all_day IN (0,1)),
    title       TEXT    NOT NULL DEFAULT '',
    notes       TEXT    NOT NULL DEFAULT '',
    color_idx   INTEGER NOT NULL DEFAULT 0   CHECK (color_idx >= 0 AND color_idx < 16),
    remind_min  INTEGER NOT NULL DEFAULT -1,               -- minutos antes del start; -1 = sin aviso
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_events_day ON planner_events(ymd) WHERE deleted_at IS NULL;

-- ── Tareas / TODOs ───────────────────────────────────────────────────────────
CREATE TABLE planner_tasks (
    id          TEXT PRIMARY KEY,
    due_ymd     TEXT,                                      -- nullable: NULL = backlog/algún día
    list        TEXT    NOT NULL DEFAULT 'todo' CHECK (list IN ('focus','priority','todo','month')),
    title       TEXT    NOT NULL DEFAULT '',
    done        INTEGER NOT NULL DEFAULT 0   CHECK (done IN (0,1)),
    priority    INTEGER NOT NULL DEFAULT 0   CHECK (priority >= 0 AND priority < 4),
    order_idx   REAL    NOT NULL DEFAULT 0,
    remind_at   INTEGER,                                   -- epoch (s); NULL = sin aviso
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_tasks_day  ON planner_tasks(due_ymd)       WHERE deleted_at IS NULL;
CREATE INDEX idx_planner_tasks_open ON planner_tasks(done, due_ymd) WHERE deleted_at IS NULL;

-- ── Hábitos + marcas ─────────────────────────────────────────────────────────
CREATE TABLE planner_habits (
    id          TEXT PRIMARY KEY,
    name        TEXT    NOT NULL DEFAULT '',
    order_idx   REAL    NOT NULL DEFAULT 0,
    archived    INTEGER NOT NULL DEFAULT 0   CHECK (archived IN (0,1)),
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER
);
CREATE TABLE planner_habit_marks (
    habit_id    TEXT    NOT NULL,
    ymd         TEXT    NOT NULL,
    value       INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (habit_id, ymd)
);

-- ── Tinta nativa: enganche genérico page_key → página de un scratch oculto ────
-- (mismo patrón PUNTERO que post-its; xochitl persiste el .lines en el scratch)
CREATE TABLE planner_ink (
    page_key       TEXT PRIMARY KEY,                       -- p.ej. 'day-2026-06-24-sk', 'note-007'
    scratch_doc_id TEXT NOT NULL,
    scratch_page   TEXT NOT NULL,
    created_at     INTEGER NOT NULL DEFAULT (unixepoch())
);

-- ── Extras estructuradas genéricas (goals/budget/colecciones/reviews) ────────
-- Las fases F5 tipan kind+bucket; F1 sólo crea la tabla y el CRUD genérico.
CREATE TABLE planner_entries (
    id          TEXT PRIMARY KEY,
    kind        TEXT    NOT NULL,                           -- 'goal'|'budget'|'collection'|'review'|…
    bucket      TEXT    NOT NULL DEFAULT '',                -- 'ym=2026-06'|'coll=books'|'period=Q2'…
    title       TEXT    NOT NULL DEFAULT '',
    body        TEXT    NOT NULL DEFAULT '',
    num         REAL,                                       -- numérico opcional (monto, etc.)
    done        INTEGER NOT NULL DEFAULT 0   CHECK (done IN (0,1)),
    order_idx   REAL    NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_entries_bucket ON planner_entries(kind, bucket) WHERE deleted_at IS NULL;

-- ── Triggers updated_at (mismo patrón que postits/settings) ──────────────────
CREATE TRIGGER trg_planner_events_updated AFTER UPDATE ON planner_events
WHEN OLD.updated_at = NEW.updated_at
BEGIN UPDATE planner_events SET updated_at = unixepoch() WHERE id = NEW.id; END;

CREATE TRIGGER trg_planner_tasks_updated AFTER UPDATE ON planner_tasks
WHEN OLD.updated_at = NEW.updated_at
BEGIN UPDATE planner_tasks SET updated_at = unixepoch() WHERE id = NEW.id; END;

CREATE TRIGGER trg_planner_habits_updated AFTER UPDATE ON planner_habits
WHEN OLD.updated_at = NEW.updated_at
BEGIN UPDATE planner_habits SET updated_at = unixepoch() WHERE id = NEW.id; END;

CREATE TRIGGER trg_planner_entries_updated AFTER UPDATE ON planner_entries
WHEN OLD.updated_at = NEW.updated_at
BEGIN UPDATE planner_entries SET updated_at = unixepoch() WHERE id = NEW.id; END;

PRAGMA user_version = 6;
