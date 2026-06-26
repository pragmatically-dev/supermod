#include "LinksRepo.hpp"
#include "Database.hpp"
#include "RowMarshaller.hpp"

#include <sqlite3.h>
#include <cstdio>
#include <QSet>
#include <QUuid>

namespace {

bool bindText(sqlite3_stmt *stmt, int idx, const QString &s) {
    if (s.isNull()) return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    const QByteArray utf8 = s.toUtf8();
    return sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindOptText(sqlite3_stmt *stmt, int idx, const QVariant &v) {
    if (!v.isValid() || v.isNull()) return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    return bindText(stmt, idx, v.toString());
}

void logErr(sqlite3 *db, const char *op) {
    std::fprintf(stderr, "[chiplinks-backend] %s failed: %s\n", op,
                 db ? sqlite3_errmsg(db) : "(no db)");
}

}

QVariantList LinksRepo::loadForDoc(const QString &sourceDocId) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT id, source_doc_id, target_doc_id, target_page_key, name, metadata,
               created_at, updated_at
        FROM links
        WHERE source_doc_id = ? AND deleted_at IS NULL
        ORDER BY updated_at DESC;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::loadForDoc prepare");
        return out;
    }
    bindText(stmt, 1, sourceDocId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QString LinksRepo::mintId() {
    return QStringLiteral("xref:") + QUuid::createUuid().toString(QUuid::Id128);
}

bool LinksRepo::upsert(const QVariantMap &link) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    static const char *kSql = R"sql(
        INSERT INTO links (id, source_doc_id, target_doc_id, target_page_key, name, metadata,
                           source_page, target_page_label)
        VALUES (?, ?, ?, ?, ?, COALESCE(?, '{}'), ?, COALESCE(?, ''))
        ON CONFLICT(id) DO UPDATE SET
            source_doc_id     = excluded.source_doc_id,
            target_doc_id     = excluded.target_doc_id,
            target_page_key   = excluded.target_page_key,
            name              = excluded.name,
            metadata          = excluded.metadata,
            source_page       = COALESCE(excluded.source_page, links.source_page),
            target_page_label = CASE WHEN excluded.target_page_label = ''
                                     THEN links.target_page_label
                                     ELSE excluded.target_page_label END,
            deleted_at        = NULL;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::upsert prepare");
        return false;
    }
    bindText   (stmt, 1, link.value(QStringLiteral("id")).toString());
    bindText   (stmt, 2, link.value(QStringLiteral("source_doc_id")).toString());
    bindText   (stmt, 3, link.value(QStringLiteral("target_doc_id")).toString());
    bindOptText(stmt, 4, link.value(QStringLiteral("target_page_key")));
    bindText   (stmt, 5, link.value(QStringLiteral("name")).toString());
    bindOptText(stmt, 6, link.value(QStringLiteral("metadata")));
    const QVariant sp = link.value(QStringLiteral("source_page"));
    if (sp.isValid() && !sp.isNull()) sqlite3_bind_int(stmt, 7, sp.toInt());
    else                              sqlite3_bind_null(stmt, 7);
    bindOptText(stmt, 8, link.value(QStringLiteral("target_page_label")));

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "LinksRepo::upsert step");
    sqlite3_finalize(stmt);
    return ok;
}

bool LinksRepo::remove(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE links SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::remove prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool LinksRepo::restore(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE links SET deleted_at = NULL WHERE id = ? AND deleted_at IS NOT NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::restore prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool LinksRepo::rename(const QString &id, const QString &name) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE links SET name = ? WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::rename prepare");
        return false;
    }
    bindText(stmt, 1, name);
    bindText(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

QString LinksRepo::sourceDocIdForLink(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return {};
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "SELECT source_doc_id FROM links WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::sourceDocIdForLink prepare");
        return {};
    }
    bindText(stmt, 1, id);
    QString out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt) out = QString::fromUtf8(reinterpret_cast<const char *>(txt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList LinksRepo::search(const QString &query, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle || query.isEmpty()) return out;

    static const char *kSql = R"sql(
        SELECT l.id, l.source_doc_id, l.target_doc_id, l.target_page_key, l.name,
               l.metadata, l.created_at, l.updated_at, bm25(links_fts) AS rank
        FROM links_fts
        JOIN links l ON l.rowid = links_fts.rowid
        WHERE links_fts MATCH ? AND l.deleted_at IS NULL
        ORDER BY rank
        LIMIT ?;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::search prepare");
        return out;
    }
    bindText(stmt, 1, query);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 20);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList LinksRepo::loadAll(bool includeDeleted, int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    const QString sql = QStringLiteral(
        "SELECT l.id, l.source_doc_id, l.target_doc_id, l.target_page_key, l.name, "
        "l.created_at, l.updated_at, l.deleted_at, "
        "sd.name AS source_doc_name, td.name AS target_doc_name "
        "FROM links l "
        "LEFT JOIN docs sd ON sd.id = l.source_doc_id "
        "LEFT JOIN docs td ON td.id = l.target_doc_id "
        "%1 ORDER BY l.updated_at DESC LIMIT ?;")
        .arg(includeDeleted ? QString() : QStringLiteral("WHERE l.deleted_at IS NULL"));
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "LinksRepo::loadAll prepare");
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 200);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantMap LinksRepo::graphData(int maxNodes) {
    QVariantMap out;
    QVariantList nodes, edges;
    out[QStringLiteral("nodes")]     = nodes;
    out[QStringLiteral("edges")]     = edges;
    out[QStringLiteral("totalDocs")] = 0;
    out[QStringLiteral("truncated")] = false;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    const int cap = maxNodes > 0 ? maxNodes : 60;

    int totalDocs = 0;
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(handle, "SELECT count(*) FROM docs WHERE deleted_at IS NULL;",
                               -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) totalDocs = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
    }
    out[QStringLiteral("totalDocs")] = totalDocs;
    out[QStringLiteral("truncated")] = (totalDocs > cap);

    QSet<QString> kept;
    static const char *kNodes = R"sql(
        WITH edge AS (
            SELECT source_doc_id AS s, target_doc_id AS t
            FROM links
            WHERE deleted_at IS NULL AND source_doc_id <> target_doc_id
            GROUP BY source_doc_id, target_doc_id
        ),
        deg AS (
            SELECT id, count(*) AS degree FROM (
                SELECT s AS id FROM edge UNION ALL SELECT t AS id FROM edge
            ) GROUP BY id
        )
        SELECT d.id, d.name, COALESCE(deg.degree, 0) AS degree
        FROM docs d
        LEFT JOIN deg ON deg.id = d.id
        WHERE d.deleted_at IS NULL
        ORDER BY degree DESC, d.last_seen_at DESC
        LIMIT ?;
    )sql";
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(handle, kNodes, -1, &st, nullptr) != SQLITE_OK) {
            logErr(handle, "LinksRepo::graphData nodes prepare");
            return out;
        }
        sqlite3_bind_int(st, 1, cap);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *id = sqlite3_column_text(st, 0);
            const unsigned char *nm = sqlite3_column_text(st, 1);
            const QString sid = id ? QString::fromUtf8(reinterpret_cast<const char *>(id)) : QString();
            QVariantMap n;
            n[QStringLiteral("id")]     = sid;
            n[QStringLiteral("name")]   = nm ? QString::fromUtf8(reinterpret_cast<const char *>(nm)) : QString();
            n[QStringLiteral("degree")] = sqlite3_column_int(st, 2);
            nodes.append(n);
            if (!sid.isEmpty()) kept.insert(sid);
        }
        sqlite3_finalize(st);
    }
    out[QStringLiteral("nodes")] = nodes;

    static const char *kEdges = R"sql(
        SELECT source_doc_id AS s, target_doc_id AS t, count(*) AS w
        FROM links
        WHERE deleted_at IS NULL AND source_doc_id <> target_doc_id
        GROUP BY source_doc_id, target_doc_id;
    )sql";
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(handle, kEdges, -1, &st, nullptr) != SQLITE_OK) {
            logErr(handle, "LinksRepo::graphData edges prepare");
            return out;
        }
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *s = sqlite3_column_text(st, 0);
            const unsigned char *t = sqlite3_column_text(st, 1);
            const QString ss = s ? QString::fromUtf8(reinterpret_cast<const char *>(s)) : QString();
            const QString tt = t ? QString::fromUtf8(reinterpret_cast<const char *>(t)) : QString();
            if (ss.isEmpty() || tt.isEmpty()) continue;
            if (!kept.contains(ss) || !kept.contains(tt)) continue;
            QVariantMap e;
            e[QStringLiteral("s")]      = ss;
            e[QStringLiteral("t")]      = tt;
            e[QStringLiteral("weight")] = sqlite3_column_int(st, 2);
            edges.append(e);
        }
        sqlite3_finalize(st);
    }
    out[QStringLiteral("edges")] = edges;
    return out;
}
