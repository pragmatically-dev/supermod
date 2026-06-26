#include "PostitsRepo.hpp"
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

const char *const kCols =
    "id, host_doc_id, host_page, x, y, scale, color_idx, "
    "scratch_doc_id, scratch_page, title, metadata, created_at, updated_at";

}

int PostitsRepo::countForScratchPages(const QString &scratchDocId, const QStringList &pageKeys) {
    sqlite3 *handle = m_db.handle();
    if (!handle || pageKeys.isEmpty()) return 0;

    QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM postits "
        "WHERE scratch_doc_id = ? AND deleted_at IS NULL AND scratch_page IN (");
    for (int i = 0; i < pageKeys.size(); ++i)
        sql += (i ? QStringLiteral(",?") : QStringLiteral("?"));
    sql += QStringLiteral(");");
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::countForScratchPages prepare");
        return 0;
    }
    bindText(stmt, 1, scratchDocId);
    for (int i = 0; i < pageKeys.size(); ++i)
        bindText(stmt, 2 + i, pageKeys.at(i));
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

QVariantList PostitsRepo::loadForPage(const QString &hostDocId, int page) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    const QString sql = QStringLiteral(
        "SELECT %1 FROM postits "
        "WHERE host_doc_id = ? AND host_page = ? AND deleted_at IS NULL "
        "ORDER BY created_at;").arg(QLatin1String(kCols));

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::loadForPage prepare");
        return out;
    }
    bindText(stmt, 1, hostDocId);
    sqlite3_bind_int(stmt, 2, page);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PostitsRepo::loadAll(bool includeDeleted, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    const QString sql = QStringLiteral(
        "SELECT %1, deleted_at FROM postits %2 ORDER BY updated_at DESC LIMIT ?;")
        .arg(QLatin1String(kCols),
             includeDeleted ? QString() : QStringLiteral("WHERE deleted_at IS NULL"));

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::loadAll prepare");
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 200);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool PostitsRepo::upsert(const QVariantMap &p) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    static const char *kSql = R"sql(
        INSERT INTO postits (id, host_doc_id, host_page, x, y, scale, color_idx,
                             scratch_doc_id, scratch_page, title)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            host_doc_id    = excluded.host_doc_id,
            host_page      = excluded.host_page,
            x              = excluded.x,
            y              = excluded.y,
            scale          = excluded.scale,
            color_idx      = excluded.color_idx,
            scratch_doc_id = excluded.scratch_doc_id,
            scratch_page   = excluded.scratch_page,
            title          = excluded.title,
            deleted_at     = NULL;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::upsert prepare");
        return false;
    }
    bindText           (stmt, 1,  p.value(QStringLiteral("id")).toString());
    bindText           (stmt, 2,  p.value(QStringLiteral("host_doc_id")).toString());
    sqlite3_bind_int   (stmt, 3,  p.value(QStringLiteral("host_page")).toInt());
    sqlite3_bind_double(stmt, 4,  p.value(QStringLiteral("x")).toDouble());
    sqlite3_bind_double(stmt, 5,  p.value(QStringLiteral("y")).toDouble());
    sqlite3_bind_double(stmt, 6,  p.value(QStringLiteral("scale"), 1.0).toDouble());
    sqlite3_bind_int   (stmt, 7,  p.value(QStringLiteral("color_idx")).toInt());
    bindText           (stmt, 8,  p.value(QStringLiteral("scratch_doc_id")).toString());
    bindText           (stmt, 9,  p.value(QStringLiteral("scratch_page")).toString());
    bindText           (stmt, 10, p.value(QStringLiteral("title")).toString());
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PostitsRepo::upsert step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PostitsRepo::setGeometry(const QString &id, qreal x, qreal y, qreal scale, int colorIdx) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql =
        "UPDATE postits SET x = ?, y = ?, scale = ?, color_idx = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::setGeometry prepare");
        return false;
    }
    sqlite3_bind_double(stmt, 1, x);
    sqlite3_bind_double(stmt, 2, y);
    sqlite3_bind_double(stmt, 3, scale);
    sqlite3_bind_int   (stmt, 4, colorIdx);
    bindText           (stmt, 5, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PostitsRepo::setGeometry step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PostitsRepo::setTitle(const QString &id, const QString &title) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "UPDATE postits SET title = ? WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::setTitle prepare");
        return false;
    }
    bindText(stmt, 1, title);
    bindText(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool PostitsRepo::markDeleted(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE postits SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::markDeleted prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool PostitsRepo::restore(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "UPDATE postits SET deleted_at = NULL WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::restore prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QString PostitsRepo::hostDocIdForPostit(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return QString();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT host_doc_id FROM postits WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PostitsRepo::hostDocIdForPostit prepare");
        return QString();
    }
    bindText(stmt, 1, id);
    QString out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *t = sqlite3_column_text(stmt, 0);
        if (t) out = QString::fromUtf8(reinterpret_cast<const char *>(t));
    }
    sqlite3_finalize(stmt);
    return out;
}
