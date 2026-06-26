#include "BacklinksRepo.hpp"
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

QVariantList BacklinksRepo::query(const QString &targetDocId, const QString &targetPageKey) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    const bool allPages = targetPageKey.isEmpty();

    const char *kSqlAll = R"sql(
        SELECT b.link_id, b.source_doc_id, b.source_doc_name, b.target_doc_id,
               b.target_page_key, b.target_doc_name, b.link_name, b.updated_at,
               b.metadata, p.source_page AS source_page
        FROM backlinks b
        LEFT JOIN pins p ON p.link_id = b.link_id
        WHERE b.target_doc_id = ?
        ORDER BY b.source_doc_name COLLATE NOCASE, p.source_page;
    )sql";
    const char *kSqlExact = R"sql(
        SELECT b.link_id, b.source_doc_id, b.source_doc_name, b.target_doc_id,
               b.target_page_key, b.target_doc_name, b.link_name, b.updated_at,
               b.metadata, p.source_page AS source_page
        FROM backlinks b
        LEFT JOIN pins p ON p.link_id = b.link_id
        WHERE b.target_doc_id = ?
          AND COALESCE(b.target_page_key, '') = COALESCE(?, '')
        ORDER BY b.source_doc_name COLLATE NOCASE, p.source_page;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, allPages ? kSqlAll : kSqlExact, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "BacklinksRepo::query prepare");
        return out;
    }
    bindText(stmt, 1, targetDocId);
    if (!allPages) bindText(stmt, 2, targetPageKey);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList BacklinksRepo::groupsForDoc(const QString &targetDocId, const QString &targetPageKey) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT b.source_doc_id, b.source_doc_name,
               COALESCE(b.source_page, -1)  AS source_page,
               COUNT(*)                     AS link_count,
               group_concat(b.link_id)      AS link_ids,
               MAX(b.link_name)             AS link_name,
               MAX(json_extract(b.metadata, '$.source_page_key')) AS source_page_key
        FROM backlinks b
        WHERE b.target_doc_id = ?1
          AND (?2 = '' OR COALESCE(b.target_page_key, '') = ?2)
        GROUP BY b.source_doc_id, COALESCE(b.source_page, -1)
        ORDER BY b.source_doc_name COLLATE NOCASE, b.source_doc_id, source_page;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "BacklinksRepo::groupsForDoc prepare");
        return out;
    }
    bindText(stmt, 1, targetDocId);
    bindText(stmt, 2, targetPageKey.isNull() ? QStringLiteral("") : targetPageKey);

    QString curDoc;
    QVariantMap group;
    QVariantList placements;
    auto flush = [&]() {
        if (curDoc.isEmpty()) return;
        group[QStringLiteral("placements")] = placements;
        out.append(group);
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QVariantMap row = RowMarshaller::toMap(stmt);
        const QString docId = row.value(QStringLiteral("source_doc_id")).toString();
        if (docId != curDoc) {
            flush();
            curDoc = docId;
            placements = QVariantList();
            group = QVariantMap();
            group[QStringLiteral("source_doc_id")]   = docId;
            group[QStringLiteral("source_doc_name")] = row.value(QStringLiteral("source_doc_name"));
        }
        QVariantMap p;
        p[QStringLiteral("source_page")]     = row.value(QStringLiteral("source_page"));
        p[QStringLiteral("link_count")]      = row.value(QStringLiteral("link_count"));
        p[QStringLiteral("link_ids")]        = row.value(QStringLiteral("link_ids"));
        p[QStringLiteral("link_name")]       = row.value(QStringLiteral("link_name"));
        p[QStringLiteral("source_page_key")] = row.value(QStringLiteral("source_page_key"));
        placements.append(p);
    }
    flush();
    sqlite3_finalize(stmt);
    return out;
}

int BacklinksRepo::count(const QString &targetDocId, const QString &targetPageKey) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return 0;

    const bool allPages = targetPageKey.isEmpty();
    const char *kSqlAll   = "SELECT count(*) FROM backlinks WHERE target_doc_id = ?;";
    const char *kSqlExact = R"sql(
        SELECT count(*) FROM backlinks
        WHERE target_doc_id = ?
          AND COALESCE(target_page_key, '') = COALESCE(?, '');
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, allPages ? kSqlAll : kSqlExact, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "BacklinksRepo::count prepare");
        return 0;
    }
    bindText(stmt, 1, targetDocId);
    if (!allPages) bindText(stmt, 2, targetPageKey);
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}
