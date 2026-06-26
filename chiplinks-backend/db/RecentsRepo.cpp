#include "RecentsRepo.hpp"
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

QVariantList RecentsRepo::top(int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT r.target_doc_id, r.target_page_key, r.last_used_at, r.use_count,
               d.name AS target_doc_name
        FROM recents r
        JOIN docs d ON d.id = r.target_doc_id AND d.deleted_at IS NULL
        ORDER BY r.last_used_at DESC
        LIMIT ?;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "RecentsRepo::top prepare");
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 20);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList RecentsRepo::topForDoc(const QString &srcDocId, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT r.target_doc_id, r.target_page_key, r.last_used_at, r.use_count,
               d.name AS target_doc_name,
               EXISTS(SELECT 1 FROM links l
                       WHERE l.source_doc_id = ? AND l.deleted_at IS NULL
                         AND l.target_doc_id = r.target_doc_id
                         AND COALESCE(l.target_page_key,'') = COALESCE(r.target_page_key,'')) AS inDoc
        FROM recents r
        JOIN docs d ON d.id = r.target_doc_id AND d.deleted_at IS NULL
        ORDER BY r.last_used_at DESC
        LIMIT ?;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "RecentsRepo::topForDoc prepare");
        return out;
    }
    bindText(stmt, 1, srcDocId);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 20);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool RecentsRepo::forget(const QString &targetDocId, const QString &targetPageKey) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "DELETE FROM recents WHERE target_doc_id = ? AND target_page_key = COALESCE(?, '');",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "RecentsRepo::forget prepare");
        return false;
    }
    bindText(stmt, 1, targetDocId);
    bindText(stmt, 2, targetPageKey);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool RecentsRepo::bump(const QString &targetDocId, const QString &targetPageKey,
                       const QString &targetPageLabel) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    const QString pageKey = targetPageKey.isNull() ? QString() : targetPageKey;
    const QString label   = targetPageLabel.isNull() ? QString() : targetPageLabel;

    static const char *kSql = R"sql(
        INSERT INTO recents (target_doc_id, target_page_key, last_used_at, use_count, target_page_label)
        VALUES (?, COALESCE(?, ''), unixepoch(), 1, COALESCE(?, ''))
        ON CONFLICT(target_doc_id, target_page_key) DO UPDATE SET
            last_used_at      = unixepoch(),
            use_count         = use_count + 1,
            target_page_label = CASE WHEN excluded.target_page_label = ''
                                     THEN recents.target_page_label
                                     ELSE excluded.target_page_label END;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "RecentsRepo::bump prepare");
        return false;
    }
    bindText(stmt, 1, targetDocId);
    bindText(stmt, 2, pageKey);
    bindText(stmt, 3, label);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "RecentsRepo::bump step");
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList RecentsRepo::groups(const QString &srcDocId, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT r.target_doc_id, r.target_page_key, r.target_page_label,
               r.use_count, r.last_used_at, d.name AS target_doc_name,
               EXISTS(SELECT 1 FROM links l
                       WHERE l.source_doc_id = ? AND l.deleted_at IS NULL
                         AND l.target_doc_id = r.target_doc_id
                         AND COALESCE(l.target_page_key,'') = COALESCE(r.target_page_key,'')) AS in_doc
        FROM recents r
        JOIN docs d ON d.id = r.target_doc_id AND d.deleted_at IS NULL
        ORDER BY d.name COLLATE NOCASE, r.target_doc_id, r.last_used_at DESC
        LIMIT ?;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "RecentsRepo::groups prepare");
        return out;
    }
    bindText(stmt, 1, srcDocId);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 200);

    QString curDoc;
    QVariantMap group;
    QVariantList pages;
    int total = 0;
    auto flush = [&]() {
        if (curDoc.isEmpty()) return;
        group[QStringLiteral("total_use_count")] = total;
        group[QStringLiteral("pages")] = pages;
        out.append(group);
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QVariantMap row = RowMarshaller::toMap(stmt);
        const QString docId = row.value(QStringLiteral("target_doc_id")).toString();
        if (docId != curDoc) {
            flush();
            curDoc = docId;
            total = 0;
            pages = QVariantList();
            group = QVariantMap();
            group[QStringLiteral("target_doc_id")]   = docId;
            group[QStringLiteral("target_doc_name")] = row.value(QStringLiteral("target_doc_name"));
        }
        const int uc = row.value(QStringLiteral("use_count")).toInt();
        total += uc;
        QVariantMap page;
        page[QStringLiteral("target_page_key")]   = row.value(QStringLiteral("target_page_key"));
        page[QStringLiteral("target_page_label")] = row.value(QStringLiteral("target_page_label"));
        page[QStringLiteral("use_count")]         = uc;
        page[QStringLiteral("last_used_at")]      = row.value(QStringLiteral("last_used_at"));
        page[QStringLiteral("in_doc")]            = row.value(QStringLiteral("in_doc")).toInt() != 0;
        pages.append(page);
    }
    flush();
    sqlite3_finalize(stmt);
    return out;
}
