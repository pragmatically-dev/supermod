#include "Database.hpp"

#include <sqlite3.h>

#include <QDir>
#include <QFileInfo>
#include <cstdio>

namespace {

const char *const kSchemaV1 = R"sql(
PRAGMA foreign_keys = ON;

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
CREATE INDEX idx_docs_alive  ON docs(name)              WHERE deleted_at IS NULL;
CREATE INDEX idx_docs_recent ON docs(last_seen_at DESC) WHERE deleted_at IS NULL;

CREATE TRIGGER trg_docs_updated AFTER UPDATE ON docs
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE docs SET updated_at = unixepoch() WHERE id = NEW.id;
END;

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

CREATE TABLE recents (
    target_doc_id   TEXT NOT NULL REFERENCES docs(id) ON DELETE CASCADE,
    target_page_key TEXT NOT NULL DEFAULT '',
    last_used_at    INTEGER NOT NULL,
    use_count       INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (target_doc_id, target_page_key)
);
CREATE INDEX idx_recents_at ON recents(last_used_at DESC);

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

PRAGMA user_version = 1;
)sql";

const char *const kMigrationV2 = R"sql(
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
)sql";

const char *const kMigrationV3 = R"sql(
DROP VIEW IF EXISTS backlinks;
CREATE VIEW backlinks AS
SELECT
    l.id               AS link_id,
    l.source_doc_id,
    sd.name            AS source_doc_name,
    l.target_doc_id,
    l.target_page_key,
    td.name            AS target_doc_name,
    l.name             AS link_name,
    l.metadata,
    l.updated_at
FROM links l
JOIN docs sd ON sd.id = l.source_doc_id AND sd.deleted_at IS NULL
JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
WHERE l.deleted_at IS NULL;
PRAGMA user_version = 3;
)sql";

const char *const kMigrationV4 = R"sql(
ALTER TABLE links ADD COLUMN source_page INTEGER;
ALTER TABLE links ADD COLUMN target_page_label TEXT NOT NULL DEFAULT '';

UPDATE links SET source_page = (
    SELECT p.source_page FROM pins p
    WHERE p.link_id = links.id
    ORDER BY p.source_page LIMIT 1
) WHERE source_page IS NULL;

UPDATE links SET target_page_key = '' WHERE target_page_key IS NULL;

ALTER TABLE recents ADD COLUMN target_page_label TEXT NOT NULL DEFAULT '';

DROP VIEW IF EXISTS backlinks;
CREATE VIEW backlinks AS
SELECT
    l.id               AS link_id,
    l.source_doc_id,
    sd.name            AS source_doc_name,
    l.source_page,
    l.target_doc_id,
    l.target_page_key,
    td.name            AS target_doc_name,
    l.name             AS link_name,
    l.metadata,
    l.updated_at
FROM links l
JOIN docs sd ON sd.id = l.source_doc_id AND sd.deleted_at IS NULL
JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
WHERE l.deleted_at IS NULL
  AND l.source_doc_id <> l.target_doc_id;
PRAGMA user_version = 4;
)sql";

const char *const kMigrationV5 = R"sql(
CREATE TABLE settings (
    key        TEXT PRIMARY KEY,
    section    TEXT NOT NULL,
    value      TEXT NOT NULL,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX idx_settings_section ON settings(section);

CREATE TRIGGER trg_settings_updated AFTER UPDATE ON settings
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE settings SET updated_at = unixepoch() WHERE key = NEW.key;
END;
PRAGMA user_version = 5;
)sql";

const char *const kMigrationV6 = R"sql(
CREATE TABLE planner_events (
    id          TEXT PRIMARY KEY,
    ymd         TEXT    NOT NULL,
    start_min   INTEGER NOT NULL DEFAULT 0   CHECK (start_min >= 0 AND start_min <  1440),
    end_min     INTEGER NOT NULL DEFAULT 0   CHECK (end_min   >= 0 AND end_min   <= 1440),
    all_day     INTEGER NOT NULL DEFAULT 0   CHECK (all_day IN (0,1)),
    title       TEXT    NOT NULL DEFAULT '',
    notes       TEXT    NOT NULL DEFAULT '',
    color_idx   INTEGER NOT NULL DEFAULT 0   CHECK (color_idx >= 0 AND color_idx < 16),
    remind_min  INTEGER NOT NULL DEFAULT -1,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_events_day ON planner_events(ymd) WHERE deleted_at IS NULL;

CREATE TABLE planner_tasks (
    id          TEXT PRIMARY KEY,
    due_ymd     TEXT,
    list        TEXT    NOT NULL DEFAULT 'todo' CHECK (list IN ('focus','priority','todo','month')),
    title       TEXT    NOT NULL DEFAULT '',
    done        INTEGER NOT NULL DEFAULT 0   CHECK (done IN (0,1)),
    priority    INTEGER NOT NULL DEFAULT 0   CHECK (priority >= 0 AND priority < 4),
    order_idx   REAL    NOT NULL DEFAULT 0,
    remind_at   INTEGER,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_tasks_day  ON planner_tasks(due_ymd)       WHERE deleted_at IS NULL;
CREATE INDEX idx_planner_tasks_open ON planner_tasks(done, due_ymd) WHERE deleted_at IS NULL;

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

CREATE TABLE planner_ink (
    page_key       TEXT PRIMARY KEY,
    scratch_doc_id TEXT NOT NULL,
    scratch_page   TEXT NOT NULL,
    created_at     INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE planner_entries (
    id          TEXT PRIMARY KEY,
    kind        TEXT    NOT NULL,
    bucket      TEXT    NOT NULL DEFAULT '',
    title       TEXT    NOT NULL DEFAULT '',
    body        TEXT    NOT NULL DEFAULT '',
    num         REAL,
    done        INTEGER NOT NULL DEFAULT 0   CHECK (done IN (0,1)),
    order_idx   REAL    NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    deleted_at  INTEGER,
    metadata    TEXT    NOT NULL DEFAULT '{}'
);
CREATE INDEX idx_planner_entries_bucket ON planner_entries(kind, bucket) WHERE deleted_at IS NULL;

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
)sql";

const char *const kMigrationV7 = R"sql(
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
)sql";

const char *const kPragmasOnOpen = R"sql(
PRAGMA busy_timeout = 4000;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA auto_vacuum = INCREMENTAL;
)sql";

void logErr(const char *op, const char *msg) {
    std::fprintf(stderr, "[chiplinks-backend] db error in %s: %s\n", op, msg ? msg : "(null)");
}

}

Database::Database() = default;

Database::~Database() { close(); }

bool Database::open(const QString &path) {
    m_path = path;

    QFileInfo info(path);
    QDir parent = info.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        m_lastError = QStringLiteral("cannot mkpath %1").arg(parent.path());
        logErr("open", m_lastError.toUtf8().constData());
        return false;
    }

    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        logErr("sqlite3_open_v2", m_lastError.toUtf8().constData());
        close();
        return false;
    }

    if (!execScript(kPragmasOnOpen)) {
        close();
        return false;
    }

    std::fprintf(stderr, "[chiplinks-backend] db opened at %s (sqlite %s)\n",
                 path.toUtf8().constData(), sqlite3_libversion());

    {
        long long cacheSize = 0, pageSize = 0;
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(m_db, "PRAGMA cache_size;", -1, &st, nullptr) == SQLITE_OK &&
            sqlite3_step(st) == SQLITE_ROW) {
            cacheSize = sqlite3_column_int64(st, 0);
        }
        sqlite3_finalize(st);
        st = nullptr;
        if (sqlite3_prepare_v2(m_db, "PRAGMA page_size;", -1, &st, nullptr) == SQLITE_OK &&
            sqlite3_step(st) == SQLITE_ROW) {
            pageSize = sqlite3_column_int64(st, 0);
        }
        sqlite3_finalize(st);
        std::fprintf(stderr, "[chiplinks-perf] sqlite cache_size=%lld page_size=%lld\n",
                     cacheSize, pageSize);
    }
    return true;
}

void Database::close() {
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

int Database::userVersion() {
    if (!m_db) return -1;
    sqlite3_stmt *stmt = nullptr;
    const int rc = sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        logErr("userVersion prepare", m_lastError.toUtf8().constData());
        return -1;
    }
    int version = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

bool Database::runInitialMigrationIfNeeded() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 1) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v1\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kSchemaV1)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema applied, user_version=%d\n", after);
    return after == 1;
}

bool Database::migrateToV2() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 2) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=2)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v2 (postits)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV2)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v2 applied, user_version=%d\n", after);
    return after == 2;
}

bool Database::migrateToV3() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 3) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=3)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v3 (backlinks VIEW + metadata)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV3)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v3 applied, user_version=%d\n", after);
    return after == 3;
}

bool Database::migrateToV4() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 4) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=4)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v4 (cross-doc grouping: links.source_page/target_page_label + recents.target_page_label + backlinks VIEW)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV4)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v4 applied, user_version=%d\n", after);
    return after == 4;
}

bool Database::migrateToV5() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 5) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=5)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v5 (SuperMod global settings table)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV5)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v5 applied, user_version=%d\n", after);
    return after == 5;
}

bool Database::migrateToV6() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 6) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=6)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v6 (Supermod Planner: events/tasks/habits/ink/entries)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV6)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v6 applied, user_version=%d\n", after);
    return after == 6;
}

bool Database::migrateToV7() {
    if (!m_db) return false;
    const int current = userVersion();
    if (current < 0) return false;
    if (current >= 7) {
        std::fprintf(stderr, "[chiplinks-backend] schema already at version %d (>=7)\n", current);
        return true;
    }

    std::fprintf(stderr, "[chiplinks-backend] applying schema v7 (planner notification center: planner_notifications)\n");

    if (!exec(QStringLiteral("BEGIN IMMEDIATE;"))) return false;
    if (!execScript(kMigrationV7)) {
        exec(QStringLiteral("ROLLBACK;"));
        return false;
    }
    if (!exec(QStringLiteral("COMMIT;"))) return false;

    const int after = userVersion();
    std::fprintf(stderr, "[chiplinks-backend] schema v7 applied, user_version=%d\n", after);
    return after == 7;
}

QVariantMap Database::countRows() {
    QVariantMap out;
    if (!m_db) return out;
    static const char *const tables[] = {"docs", "toc_items", "links", "pins", "recents", "meta", "postits", nullptr};
    for (int i = 0; tables[i]; ++i) {
        const QString sql = QStringLiteral("SELECT count(*) FROM %1;").arg(QLatin1String(tables[i]));
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
            out[QLatin1String(tables[i])] = -1;
            continue;
        }
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out[QLatin1String(tables[i])] = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

int Database::purgeSoftDeleted(int days) {
    if (!m_db || days < 0) return 0;
    static const char *const kTables[] = {"docs", "toc_items", "links", nullptr};
    int totalDeleted = 0;
    for (int i = 0; kTables[i]; ++i) {
        const QString sql = QStringLiteral(
            "DELETE FROM %1 WHERE deleted_at IS NOT NULL AND deleted_at < unixepoch() - ?;")
            .arg(QLatin1String(kTables[i]));
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
            logErr("purgeSoftDeleted prepare", sqlite3_errmsg(m_db));
            continue;
        }
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(days) * 86400);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            totalDeleted += sqlite3_changes(m_db);
        }
        sqlite3_finalize(stmt);
    }
    std::fprintf(stderr, "[chiplinks-backend] purgeSoftDeleted(%d days): %d rows deleted\n",
                 days, totalDeleted);
    return totalDeleted;
}

bool Database::incrementalVacuum() {
    if (!m_db) return false;
    const bool ok = execScript("PRAGMA incremental_vacuum;");
    if (ok) std::fprintf(stderr, "[chiplinks-backend] incremental_vacuum OK\n");
    return ok;
}

bool Database::beginTransaction() { return execScript("BEGIN IMMEDIATE;"); }
bool Database::commit()           { return execScript("COMMIT;"); }
bool Database::rollback()         { return execScript("ROLLBACK;"); }

bool Database::exec(const QString &sql) {
    return execScript(sql.toUtf8().constData());
}

bool Database::execScript(const char *sql) {
    if (!m_db || !sql) return false;
    char *err = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        m_lastError = QString::fromUtf8(err ? err : sqlite3_errmsg(m_db));
        logErr("sqlite3_exec", m_lastError.toUtf8().constData());
        sqlite3_free(err);
        return false;
    }
    return true;
}

int Database::resetAll() {
    if (!m_db) return -1;

    static const char *const kOrder[] = {
        "postits", "pins", "links", "toc_items", "recents", "docs", "meta", nullptr
    };
    if (!beginTransaction()) return -1;
    int total = 0;
    for (int i = 0; kOrder[i]; ++i) {
        const QString sql = QStringLiteral("DELETE FROM %1;").arg(QLatin1String(kOrder[i]));
        if (!exec(sql)) { rollback(); return -1; }
        total += sqlite3_changes(m_db);
    }
    if (!commit()) { rollback(); return -1; }
    incrementalVacuum();
    std::fprintf(stderr, "[chiplinks-backend] resetAll: %d rows deleted\n", total);
    return total;
}

QVariantMap Database::stats(int topN) {
    QVariantMap out;
    if (!m_db) return out;
    struct Q { const char *key; const char *sql; };
    static const Q kScalars[] = {
        {"docs_alive",         "SELECT count(*) FROM docs WHERE deleted_at IS NULL;"},
        {"links_alive",        "SELECT count(*) FROM links WHERE deleted_at IS NULL;"},
        {"crossDocEdges",      "SELECT count(*) FROM (SELECT DISTINCT source_doc_id,target_doc_id "
                               "FROM links WHERE deleted_at IS NULL AND source_doc_id<>target_doc_id);"},
        {"docs_with_outlinks", "SELECT count(DISTINCT source_doc_id) FROM links WHERE deleted_at IS NULL;"},
        {"orphanDocs",         "SELECT count(*) FROM docs d WHERE d.deleted_at IS NULL AND NOT EXISTS "
                               "(SELECT 1 FROM links l WHERE l.deleted_at IS NULL "
                               "AND (l.source_doc_id=d.id OR l.target_doc_id=d.id));"},
        {"dangling_links",     "SELECT count(*) FROM links l WHERE l.deleted_at IS NULL AND NOT EXISTS "
                               "(SELECT 1 FROM docs d WHERE d.id=l.target_doc_id AND d.deleted_at IS NULL);"},
        {"pins_total",         "SELECT count(*) FROM pins;"},
        {"postits_total",      "SELECT count(*) FROM postits WHERE deleted_at IS NULL;"},
        {"toc_alive",          "SELECT count(*) FROM toc_items WHERE deleted_at IS NULL;"},
        {"recents_total",      "SELECT count(*) FROM recents;"},
        {"soft_deleted_total", "SELECT (SELECT count(*) FROM docs WHERE deleted_at IS NOT NULL)"
                               "+(SELECT count(*) FROM links WHERE deleted_at IS NOT NULL)"
                               "+(SELECT count(*) FROM toc_items WHERE deleted_at IS NOT NULL);"},
        {nullptr, nullptr}
    };
    for (int i = 0; kScalars[i].key; ++i) {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(m_db, kScalars[i].sql, -1, &st, nullptr) != SQLITE_OK) {
            out[QLatin1String(kScalars[i].key)] = -1;
            continue;
        }
        if (sqlite3_step(st) == SQLITE_ROW) out[QLatin1String(kScalars[i].key)] = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    QVariantList top;
    static const char *kTop =
        "SELECT d.id,d.name,count(*) AS links FROM links l "
        "JOIN docs d ON d.id=l.target_doc_id AND d.deleted_at IS NULL "
        "WHERE l.deleted_at IS NULL GROUP BY l.target_doc_id "
        "ORDER BY links DESC, d.name LIMIT ?;";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, kTop, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, topN > 0 ? topN : 5);
        while (sqlite3_step(st) == SQLITE_ROW) {
            QVariantMap r;
            const unsigned char *id = sqlite3_column_text(st, 0);
            const unsigned char *nm = sqlite3_column_text(st, 1);
            r["id"]    = id ? QString::fromUtf8(reinterpret_cast<const char *>(id)) : QString();
            r["name"]  = nm ? QString::fromUtf8(reinterpret_cast<const char *>(nm)) : QString();
            r["links"] = sqlite3_column_int(st, 2);
            top.append(r);
        }
        sqlite3_finalize(st);
    }
    out["topLinked"] = top;
    return out;
}
