-- =============================================================================
-- chiplinks-backend schema v1
--
-- Diseño:
--   docs       : metadata cache de documentos vistos
--   toc_items  : índice estructural (heading tree, self-FK)
--   links      : cross-doc link definitions
--   pins       : marcadores visuales (0..N por link)
--   recents    : UX cache de últimos targets usados
--   meta       : key/value de app state
--   backlinks  : VIEW derivada de links+docs (no es tabla)
--   *_fts      : FTS5 mirrors para búsqueda fuzzy
--
-- Conventions:
--   - soft delete: cada tabla con `deleted_at INTEGER NULL`. Índices parciales
--     `WHERE deleted_at IS NULL` mantienen las queries rápidas y permiten undo.
--   - updated_at: triggers AFTER UPDATE bumpean el timestamp automáticamente.
--   - metadata JSON: cada tabla principal tiene `metadata TEXT DEFAULT '{}'`
--     para extensibilidad sin migrations futuras.
--   - CHECK constraints validan dominio (level, color_idx, page_missing).
-- =============================================================================

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- docs: cache de metadata de documentos
-- ---------------------------------------------------------------------------
CREATE TABLE docs (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    parent_id     TEXT,
    last_seen_at  INTEGER NOT NULL,
    created_at    INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at    INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at    INTEGER,
    metadata      TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_docs_alive  ON docs(name)         WHERE deleted_at IS NULL;
CREATE INDEX idx_docs_recent ON docs(last_seen_at DESC) WHERE deleted_at IS NULL;

CREATE TRIGGER trg_docs_updated AFTER UPDATE ON docs
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE docs SET updated_at = unixepoch() WHERE id = NEW.id;
END;

-- ---------------------------------------------------------------------------
-- toc_items: índice estructural del documento (heading tree)
-- ---------------------------------------------------------------------------
CREATE TABLE toc_items (
    id            TEXT PRIMARY KEY,
    doc_id        TEXT NOT NULL REFERENCES docs(id) ON DELETE CASCADE,
    parent_id     TEXT REFERENCES toc_items(id) ON DELETE CASCADE,
    name          TEXT NOT NULL,
    level         INTEGER NOT NULL DEFAULT 0 CHECK (level >= 0 AND level <= 6),
    order_idx     REAL NOT NULL,
    page_key      TEXT,
    page_missing  INTEGER NOT NULL DEFAULT 0 CHECK (page_missing IN (0, 1)),
    created_at    INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at    INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at    INTEGER,
    metadata      TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_toc_doc    ON toc_items(doc_id, order_idx)    WHERE deleted_at IS NULL;
CREATE INDEX idx_toc_parent ON toc_items(parent_id, order_idx) WHERE deleted_at IS NULL;
CREATE INDEX idx_toc_page   ON toc_items(doc_id, page_key)     WHERE deleted_at IS NULL;

CREATE TRIGGER trg_toc_items_updated AFTER UPDATE ON toc_items
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE toc_items SET updated_at = unixepoch() WHERE id = NEW.id;
END;

-- ---------------------------------------------------------------------------
-- links: cross-doc link definitions
-- ---------------------------------------------------------------------------
CREATE TABLE links (
    id              TEXT PRIMARY KEY,
    source_doc_id   TEXT NOT NULL REFERENCES docs(id) ON DELETE CASCADE,
    target_doc_id   TEXT NOT NULL REFERENCES docs(id) ON DELETE CASCADE,
    target_page_key TEXT,
    name            TEXT NOT NULL,
    target_key      TEXT GENERATED ALWAYS AS
                    (target_doc_id || ':' || COALESCE(target_page_key, '')) VIRTUAL,
    created_at      INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at      INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at      INTEGER,
    metadata        TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_links_source     ON links(source_doc_id) WHERE deleted_at IS NULL;
CREATE INDEX idx_links_target     ON links(target_key)    WHERE deleted_at IS NULL;
CREATE INDEX idx_links_target_doc ON links(target_doc_id) WHERE deleted_at IS NULL;

CREATE TRIGGER trg_links_updated AFTER UPDATE ON links
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE links SET updated_at = unixepoch() WHERE id = NEW.id;
END;

-- ---------------------------------------------------------------------------
-- pins: marcadores visuales (0..N por link)
-- ---------------------------------------------------------------------------
CREATE TABLE pins (
    link_id     TEXT NOT NULL REFERENCES links(id) ON DELETE CASCADE,
    source_page INTEGER NOT NULL CHECK (source_page >= 0),
    x           REAL NOT NULL,
    y           REAL NOT NULL,
    scale       REAL NOT NULL DEFAULT 1.0,
    color_idx   INTEGER NOT NULL DEFAULT 0 CHECK (color_idx >= 0 AND color_idx < 16),
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    metadata    TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (link_id, source_page)
);
CREATE INDEX idx_pins_page ON pins(link_id, source_page);

CREATE TRIGGER trg_pins_updated AFTER UPDATE ON pins
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE pins SET updated_at = unixepoch()
    WHERE link_id = NEW.link_id AND source_page = NEW.source_page;
END;

-- ---------------------------------------------------------------------------
-- recents: UX cache
-- ---------------------------------------------------------------------------
CREATE TABLE recents (
    target_doc_id   TEXT NOT NULL REFERENCES docs(id) ON DELETE CASCADE,
    target_page_key TEXT NOT NULL DEFAULT '',
    last_used_at    INTEGER NOT NULL,
    use_count       INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (target_doc_id, target_page_key)
);
CREATE INDEX idx_recents_at ON recents(last_used_at DESC);

-- ---------------------------------------------------------------------------
-- meta: key/value app state
-- ---------------------------------------------------------------------------
CREATE TABLE meta (
    key        TEXT PRIMARY KEY,
    value      TEXT,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TRIGGER trg_meta_updated AFTER UPDATE ON meta
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE meta SET updated_at = unixepoch() WHERE key = NEW.key;
END;

-- ---------------------------------------------------------------------------
-- backlinks: VIEW derivada (cero código de cache)
-- ---------------------------------------------------------------------------
CREATE VIEW backlinks AS
SELECT
    l.id               AS link_id,
    l.source_doc_id,
    sd.name            AS source_doc_name,
    l.target_doc_id,
    l.target_page_key,
    td.name            AS target_doc_name,
    l.name             AS link_name,
    l.updated_at
FROM links l
JOIN docs sd ON sd.id = l.source_doc_id AND sd.deleted_at IS NULL
JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
WHERE l.deleted_at IS NULL;

-- ---------------------------------------------------------------------------
-- FTS5: búsqueda fuzzy sobre toc_items.name y links.name
-- ---------------------------------------------------------------------------
CREATE VIRTUAL TABLE toc_items_fts USING fts5(
    name,
    content='toc_items',
    content_rowid='rowid',
    tokenize='unicode61'
);
CREATE TRIGGER toc_items_fts_ai AFTER INSERT ON toc_items BEGIN
    INSERT INTO toc_items_fts(rowid, name) VALUES (NEW.rowid, NEW.name);
END;
CREATE TRIGGER toc_items_fts_ad AFTER DELETE ON toc_items BEGIN
    INSERT INTO toc_items_fts(toc_items_fts, rowid, name) VALUES ('delete', OLD.rowid, OLD.name);
END;
CREATE TRIGGER toc_items_fts_au AFTER UPDATE ON toc_items BEGIN
    INSERT INTO toc_items_fts(toc_items_fts, rowid, name) VALUES ('delete', OLD.rowid, OLD.name);
    INSERT INTO toc_items_fts(rowid, name) VALUES (NEW.rowid, NEW.name);
END;

CREATE VIRTUAL TABLE links_fts USING fts5(
    name,
    content='links',
    content_rowid='rowid',
    tokenize='unicode61'
);
CREATE TRIGGER links_fts_ai AFTER INSERT ON links BEGIN
    INSERT INTO links_fts(rowid, name) VALUES (NEW.rowid, NEW.name);
END;
CREATE TRIGGER links_fts_ad AFTER DELETE ON links BEGIN
    INSERT INTO links_fts(links_fts, rowid, name) VALUES ('delete', OLD.rowid, OLD.name);
END;
CREATE TRIGGER links_fts_au AFTER UPDATE ON links BEGIN
    INSERT INTO links_fts(links_fts, rowid, name) VALUES ('delete', OLD.rowid, OLD.name);
    INSERT INTO links_fts(rowid, name) VALUES (NEW.rowid, NEW.name);
END;

-- ---------------------------------------------------------------------------
-- Schema version
-- ---------------------------------------------------------------------------
PRAGMA user_version = 1;
