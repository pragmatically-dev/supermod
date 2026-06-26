#include "MetaRepo.hpp"
#include "Database.hpp"

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

QString MetaRepo::get(const QString &key, const QString &defaultValue) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return defaultValue;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT value FROM meta WHERE key = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "MetaRepo::get prepare");
        return defaultValue;
    }
    bindText(stmt, 1, key);
    QString result = defaultValue;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (txt) result = QString::fromUtf8(txt);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool MetaRepo::set(const QString &key, const QString &value) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO meta (key, value) VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "MetaRepo::set prepare");
        return false;
    }
    bindText(stmt, 1, key);
    bindText(stmt, 2, value);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "MetaRepo::set step");
    sqlite3_finalize(stmt);
    return ok;
}
