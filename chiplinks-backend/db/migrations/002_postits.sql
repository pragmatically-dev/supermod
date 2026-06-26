-- Schema v2 — post-its (POSTIT_PATH2, native-rmlines).
--
-- Source-of-truth para la migración v1→v2. El string embebido vive en
-- Database.cpp (kMigrationV2); mantener ambos sincronizados.
--
-- Modelo PUNTERO (sin BLOB): el post-it es una FILA que apunta a una página
-- de un notebook "scratch" oculto donde xochitl persiste la tinta nativa
-- (DocumentWorker escribe el .lines solo). Acá guardamos sólo:
--   - dónde se ancla el chip (host_doc_id + host_page + x/y/scale/color)
--   - a qué página del scratch apunta (scratch_doc_id + scratch_page = page KEY)
--   - un título opcional
-- host_doc_id NO tiene FK a docs(id): el post-it sobrevive aunque el doc host
-- todavía no esté registrado en nuestra tabla docs (se registra al abrirlo).

CREATE TABLE postits (
    id             TEXT PRIMARY KEY,
    host_doc_id    TEXT NOT NULL,
    host_page      INTEGER NOT NULL DEFAULT 0 CHECK (host_page >= 0),
    x              REAL NOT NULL,
    y              REAL NOT NULL,
    scale          REAL NOT NULL DEFAULT 1.0,
    color_idx      INTEGER NOT NULL DEFAULT 0 CHECK (color_idx >= 0 AND color_idx < 16),
    scratch_doc_id TEXT NOT NULL,
    scratch_page   TEXT NOT NULL,
    title          TEXT NOT NULL DEFAULT '',
    created_at     INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at     INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at     INTEGER,
    metadata       TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_postits_host  ON postits(host_doc_id, host_page) WHERE deleted_at IS NULL;
CREATE INDEX idx_postits_alive ON postits(updated_at DESC)        WHERE deleted_at IS NULL;

CREATE TRIGGER trg_postits_updated AFTER UPDATE ON postits
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE postits SET updated_at = unixepoch() WHERE id = NEW.id;
END;

PRAGMA user_version = 2;
