#include "DocsRepo.hpp"
#include "Database.hpp"
#include "RowMarshaller.hpp"

#include <sqlite3.h>
#include <cstdio>

namespace {

bool bindText(sqlite3_stmt *stmt, int idx, const QString &s) {
    if (s.isNull()) return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    const QByteArray utf8 = s.toUtf8();
    return sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

void logErr(sqlite3 *db, const char *op) {
    std::fprintf(stderr, "[chiplinks-backend] %s failed: %s\n", op,
                 db ? sqlite3_errmsg(db) : "(no db)");
}

}

bool DocsRepo::upsert(const QString &docId, const QString &name, const QString &parentId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    static const char *kSql = R"sql(
        INSERT INTO docs (id, name, parent_id, last_seen_at)
        VALUES (?, ?, ?, unixepoch())
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            parent_id = excluded.parent_id,
            last_seen_at = excluded.last_seen_at;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "DocsRepo::upsert prepare");
        return false;
    }
    bindText(stmt, 1, docId);
    bindText(stmt, 2, name);
    bindText(stmt, 3, parentId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "DocsRepo::upsert step");
    sqlite3_finalize(stmt);
    return ok;
}

bool DocsRepo::markDeleted(const QString &docId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE docs SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "DocsRepo::markDeleted prepare");
        return false;
    }
    bindText(stmt, 1, docId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool DocsRepo::restore(const QString &docId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE docs SET deleted_at = NULL WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "DocsRepo::restore prepare");
        return false;
    }
    bindText(stmt, 1, docId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList DocsRepo::loadAll(bool includeDeleted, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    const QString sql = QStringLiteral(
        "SELECT id, name, parent_id, last_seen_at, created_at, updated_at, deleted_at "
        "FROM docs %1 ORDER BY last_seen_at DESC LIMIT ?;")
        .arg(includeDeleted ? QString() : QStringLiteral("WHERE deleted_at IS NULL"));
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "DocsRepo::loadAll prepare");
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 200);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}
