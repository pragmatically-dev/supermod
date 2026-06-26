#include "TocItemsRepo.hpp"
#include "Database.hpp"
#include "RowMarshaller.hpp"

#include <QHash>
#include <QUuid>
#include <sqlite3.h>
#include <cstdio>

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

bool rowDocAndOrder(sqlite3 *handle, const QString &id, QString &docId, double &order) {
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT doc_id, order_idx FROM toc_items WHERE id = ? AND deleted_at IS NULL;",
            -1, &st, nullptr) != SQLITE_OK) {
        logErr(handle, "rowDocAndOrder prepare");
        return false;
    }
    bindText(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        const unsigned char *d = sqlite3_column_text(st, 0);
        docId = d ? QString::fromUtf8(reinterpret_cast<const char *>(d)) : QString();
        order = sqlite3_column_double(st, 1);
    }
    sqlite3_finalize(st);
    return found;
}

bool adjacentNeighbour(sqlite3 *handle, const QString &docId, double cur, int dir,
                       QString &neighId, double &neighOrder) {
    const char *sql = (dir < 0)
        ? "SELECT id, order_idx FROM toc_items WHERE doc_id = ? AND deleted_at IS NULL "
          "AND order_idx < ? ORDER BY order_idx DESC LIMIT 1;"
        : "SELECT id, order_idx FROM toc_items WHERE doc_id = ? AND deleted_at IS NULL "
          "AND order_idx > ? ORDER BY order_idx ASC LIMIT 1;";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(handle, sql, -1, &st, nullptr) != SQLITE_OK) {
        logErr(handle, "adjacentNeighbour prepare");
        return false;
    }
    bindText(st, 1, docId);
    sqlite3_bind_double(st, 2, cur);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        const unsigned char *t = sqlite3_column_text(st, 0);
        neighId = t ? QString::fromUtf8(reinterpret_cast<const char *>(t)) : QString();
        neighOrder = sqlite3_column_double(st, 1);
    }
    sqlite3_finalize(st);
    return found;
}

bool hasTieAt(sqlite3 *handle, const QString &docId, double order, const QString &exceptId) {
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT 1 FROM toc_items WHERE doc_id = ? AND deleted_at IS NULL "
            "AND order_idx = ? AND id <> ? LIMIT 1;",
            -1, &st, nullptr) != SQLITE_OK) {
        logErr(handle, "hasTieAt prepare");
        return false;
    }
    bindText(st, 1, docId);
    sqlite3_bind_double(st, 2, order);
    bindText(st, 3, exceptId);
    const bool tie = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return tie;
}

}

QVariantList TocItemsRepo::loadForDoc(const QString &docId) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT t.id, t.doc_id, t.parent_id, t.name, t.level, t.order_idx,
               t.page_key, t.page_missing, t.metadata,
               l.target_doc_id, l.target_page_key, l.name AS link_name,
               td.name AS target_doc_name
        FROM toc_items t
        LEFT JOIN links l ON l.id = t.id AND l.deleted_at IS NULL
        LEFT JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
        WHERE t.doc_id = ? AND t.deleted_at IS NULL
        ORDER BY t.order_idx;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::loadForDoc prepare");
        return out;
    }
    bindText(stmt, 1, docId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList TocItemsRepo::loadTocCustom(const QString &docId) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT t.id, t.name, t.level, t.order_idx, t.page_key, t.page_missing,
               l.target_doc_id, l.target_page_key, td.name AS target_doc_name,
               td.deleted_at AS target_deleted_at
        FROM toc_items t
        LEFT JOIN links l ON l.id = t.id AND l.deleted_at IS NULL
        LEFT JOIN docs  td ON td.id = l.target_doc_id
        WHERE t.doc_id = ? AND t.deleted_at IS NULL
        ORDER BY t.order_idx;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::loadTocCustom prepare");
        return out;
    }
    bindText(stmt, 1, docId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QVariantMap r = RowMarshaller::toMap(stmt);
        const QString id    = r.value(QStringLiteral("id")).toString();
        const QVariant tgt  = r.value(QStringLiteral("target_doc_id"));
        const bool isXref   = id.startsWith(QStringLiteral("xref:"))
                              || (tgt.isValid() && !tgt.isNull() && !tgt.toString().isEmpty());

        QString status = QStringLiteral("available");
        if (r.value(QStringLiteral("page_missing")).toInt() != 0) {
            status = QStringLiteral("page_deleted");
        } else if (isXref) {
            const QVariant tdel = r.value(QStringLiteral("target_deleted_at"));
            const bool targetGone = !tgt.isValid() || tgt.toString().isEmpty()
                                    || r.value(QStringLiteral("target_doc_name")).isNull()
                                    || (tdel.isValid() && !tdel.isNull());
            if (targetGone) status = QStringLiteral("deleted");
        }

        QVariantMap row;
        row[QStringLiteral("pageId")]        = id;
        row[QStringLiteral("name")]          = r.value(QStringLiteral("name"));
        row[QStringLiteral("level")]         = r.value(QStringLiteral("level"));
        row[QStringLiteral("order_idx")]     = r.value(QStringLiteral("order_idx"));
        row[QStringLiteral("page_key")]      = r.value(QStringLiteral("page_key"));
        row[QStringLiteral("isUserEntry")]   = true;
        row[QStringLiteral("isCrossDoc")]    = isXref;
        row[QStringLiteral("crossDocStatus")]= status;
        row[QStringLiteral("targetDocId")]   = tgt.isValid() ? tgt.toString() : QString();
        row[QStringLiteral("targetPageKey")] = r.value(QStringLiteral("target_page_key"));
        row[QStringLiteral("targetDocName")] = r.value(QStringLiteral("target_doc_name"));
        out.append(row);
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantMap TocItemsRepo::docXrefGraph(const QString &docId) {
    QVariantMap graph;
    QVariantList nodes, edges;
    graph[QStringLiteral("nodes")] = nodes;
    graph[QStringLiteral("edges")] = edges;
    sqlite3 *handle = m_db.handle();
    if (!handle) return graph;

    QString docName;
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(handle,
                "SELECT name FROM docs WHERE id = ? AND deleted_at IS NULL;",
                -1, &st, nullptr) == SQLITE_OK) {
            bindText(st, 1, docId);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char *nm = sqlite3_column_text(st, 0);
                if (nm) docName = QString::fromUtf8(reinterpret_cast<const char *>(nm));
            }
            sqlite3_finalize(st);
        }
    }

    static const char *kSql = R"sql(
        SELECT l.target_doc_id AS id,
               td.name         AS name,
               COUNT(*)        AS linkCount,
               (SELECT t2.id FROM toc_items t2
                  JOIN links l2 ON l2.id = t2.id AND l2.deleted_at IS NULL
                 WHERE t2.doc_id = t.doc_id AND t2.deleted_at IS NULL
                   AND l2.target_doc_id = l.target_doc_id
                 ORDER BY t2.order_idx LIMIT 1)              AS xrefKey,
               (SELECT l2.target_page_key FROM toc_items t2
                  JOIN links l2 ON l2.id = t2.id AND l2.deleted_at IS NULL
                 WHERE t2.doc_id = t.doc_id AND t2.deleted_at IS NULL
                   AND l2.target_doc_id = l.target_doc_id
                 ORDER BY t2.order_idx LIMIT 1)              AS targetPageKey
        FROM toc_items t
        JOIN links l ON l.id = t.id AND l.deleted_at IS NULL
        JOIN docs  td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
        WHERE t.doc_id = ? AND t.deleted_at IS NULL
        GROUP BY l.target_doc_id;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::docXrefGraph prepare");
        return graph;
    }
    bindText(stmt, 1, docId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap n = RowMarshaller::toMap(stmt);
        n[QStringLiteral("degree")]   = n.value(QStringLiteral("linkCount"));
        n[QStringLiteral("isCenter")] = false;
        const QString tid = n.value(QStringLiteral("id")).toString();
        nodes.append(n);
        QVariantMap e;
        e[QStringLiteral("s")] = docId;
        e[QStringLiteral("t")] = tid;
        edges.append(e);
    }
    sqlite3_finalize(stmt);

    QVariantMap center;
    center[QStringLiteral("id")]       = docId;
    center[QStringLiteral("name")]     = docName;
    center[QStringLiteral("degree")]   = nodes.size();
    center[QStringLiteral("isCenter")] = true;
    QVariantList allNodes;
    allNodes.append(center);
    allNodes.append(nodes);

    graph[QStringLiteral("nodes")] = allNodes;
    graph[QStringLiteral("edges")] = edges;
    return graph;
}

bool TocItemsRepo::upsert(const QVariantMap &item) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    static const char *kSql = R"sql(
        INSERT INTO toc_items (id, doc_id, parent_id, name, level, order_idx, page_key, page_missing, metadata)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, COALESCE(?, '{}'))
        ON CONFLICT(id) DO UPDATE SET
            doc_id       = excluded.doc_id,
            parent_id    = excluded.parent_id,
            name         = excluded.name,
            level        = excluded.level,
            order_idx    = excluded.order_idx,
            page_key     = excluded.page_key,
            page_missing = excluded.page_missing,
            metadata     = excluded.metadata,
            deleted_at   = NULL;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::upsert prepare");
        return false;
    }
    bindText   (stmt, 1, item.value(QStringLiteral("id")).toString());
    bindText   (stmt, 2, item.value(QStringLiteral("doc_id")).toString());
    bindOptText(stmt, 3, item.value(QStringLiteral("parent_id")));
    bindText   (stmt, 4, item.value(QStringLiteral("name")).toString());
    sqlite3_bind_int   (stmt, 5, item.value(QStringLiteral("level"), 0).toInt());

    const QVariant ov = item.value(QStringLiteral("order_idx"));
    const bool appendSlot = !ov.isValid() || ov.isNull() || ov.toString().isEmpty();
    sqlite3_bind_double(stmt, 6, appendSlot
        ? nextOrderIdxForDoc(item.value(QStringLiteral("doc_id")).toString())
        : ov.toDouble());
    bindOptText(stmt, 7, item.value(QStringLiteral("page_key")));
    sqlite3_bind_int   (stmt, 8, item.value(QStringLiteral("page_missing"), 0).toInt() ? 1 : 0);
    bindOptText(stmt, 9, item.value(QStringLiteral("metadata")));

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "TocItemsRepo::upsert step");
    sqlite3_finalize(stmt);
    return ok;
}

bool TocItemsRepo::remove(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::remove prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool TocItemsRepo::rename(const QString &id, const QString &name) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET name = ? WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::rename prepare");
        return false;
    }
    bindText(stmt, 1, name);
    bindText(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool TocItemsRepo::setLevel(const QString &id, int level) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    const int clamped = level < 0 ? 0 : (level > 6 ? 6 : level);
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET level = ? WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::setLevel prepare");
        return false;
    }
    sqlite3_bind_int(stmt, 1, clamped);
    bindText(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool TocItemsRepo::restore(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET deleted_at = NULL WHERE id = ? AND deleted_at IS NOT NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::restore prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

int TocItemsRepo::removeMany(const QStringList &ids) {
    sqlite3 *handle = m_db.handle();
    if (!handle || ids.isEmpty()) return 0;
    if (!m_db.beginTransaction()) {
        logErr(handle, "TocItemsRepo::removeMany begin");
        return 0;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::removeMany prepare");
        m_db.rollback();
        return 0;
    }
    int n = 0;
    for (const QString &id : ids) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bindText(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_DONE) n += sqlite3_changes(handle);
    }
    sqlite3_finalize(stmt);
    if (!m_db.commit()) { m_db.rollback(); return 0; }
    return n;
}

QString TocItemsRepo::docIdForItem(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return {};
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(handle,
                           "SELECT doc_id FROM toc_items WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::docIdForItem prepare");
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

int TocItemsRepo::copyForDuplicate(const QString &sourceDocId, const QString &newDocId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return 0;

    QList<QVariantMap> rows;
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(handle,
                "SELECT id, parent_id, name, level, order_idx, page_key, page_missing, metadata "
                "FROM toc_items WHERE doc_id = ? AND deleted_at IS NULL ORDER BY level, order_idx;",
                -1, &stmt, nullptr) != SQLITE_OK) {
            logErr(handle, "copyForDuplicate select prepare");
            return 0;
        }
        bindText(stmt, 1, sourceDocId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            rows.append(RowMarshaller::toMap(stmt));
        }
        sqlite3_finalize(stmt);
    }
    if (rows.isEmpty()) return 0;

    QHash<QString, QString> idMap;
    for (const QVariantMap &row : rows) {
        const QString oldId = row.value(QStringLiteral("id")).toString();
        idMap.insert(oldId, QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    char *err = nullptr;
    if (sqlite3_exec(handle, "BEGIN IMMEDIATE;", nullptr, nullptr, &err) != SQLITE_OK) {
        logErr(handle, "copyForDuplicate BEGIN");
        sqlite3_free(err);
        return 0;
    }

    static const char *kInsert = R"sql(
        INSERT INTO toc_items (id, doc_id, parent_id, name, level, order_idx, page_key, page_missing, metadata)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql";

    sqlite3_stmt *ins = nullptr;
    if (sqlite3_prepare_v2(handle, kInsert, -1, &ins, nullptr) != SQLITE_OK) {
        logErr(handle, "copyForDuplicate insert prepare");
        sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
        return 0;
    }

    int inserted = 0;
    for (const QVariantMap &row : rows) {
        const QString newId = idMap.value(row.value(QStringLiteral("id")).toString());
        const QString oldParent = row.value(QStringLiteral("parent_id")).toString();
        const QString newParent = idMap.value(oldParent);

        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
        bindText   (ins, 1, newId);
        bindText   (ins, 2, newDocId);
        if (oldParent.isEmpty() || !idMap.contains(oldParent)) {
            sqlite3_bind_null(ins, 3);
        } else {
            bindText(ins, 3, newParent);
        }
        bindText   (ins, 4, row.value(QStringLiteral("name")).toString());
        sqlite3_bind_int   (ins, 5, row.value(QStringLiteral("level"), 0).toInt());
        sqlite3_bind_double(ins, 6, row.value(QStringLiteral("order_idx"), 0.0).toDouble());
        bindOptText(ins, 7, row.value(QStringLiteral("page_key")));
        sqlite3_bind_int   (ins, 8, row.value(QStringLiteral("page_missing"), 0).toInt() ? 1 : 0);
        bindOptText(ins, 9, row.value(QStringLiteral("metadata"), QStringLiteral("{}")));

        if (sqlite3_step(ins) != SQLITE_DONE) {
            logErr(handle, "copyForDuplicate insert step");
            sqlite3_finalize(ins);
            sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
            return 0;
        }
        ++inserted;
    }
    sqlite3_finalize(ins);

    if (sqlite3_exec(handle, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK) {
        logErr(handle, "copyForDuplicate COMMIT");
        sqlite3_free(err);
        return 0;
    }

    std::fprintf(stderr, "[chiplinks-backend] copyForDuplicate %s → %s: %d items copied\n",
                 sourceDocId.toUtf8().constData(), newDocId.toUtf8().constData(), inserted);
    return inserted;
}

bool TocItemsRepo::reorder(const QString &id, const QString &newParentId, double newOrderIdx) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE toc_items SET parent_id = ?, order_idx = ? WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::reorder prepare");
        return false;
    }
    if (newParentId.isNull()) {
        sqlite3_bind_null(stmt, 1);
    } else {
        bindText(stmt, 1, newParentId);
    }
    sqlite3_bind_double(stmt, 2, newOrderIdx);
    bindText(stmt, 3, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

double TocItemsRepo::nextOrderIdxForDoc(const QString &docId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return 1000.0;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT COALESCE(MAX(order_idx), 0.0) + 1000.0 FROM toc_items "
            "WHERE doc_id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "TocItemsRepo::nextOrderIdxForDoc prepare");
        return 1000.0;
    }
    bindText(stmt, 1, docId);
    double next = 1000.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) next = sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return next;
}

bool TocItemsRepo::moveTocItemUp(const QString &id)   { return moveOneSlot(id, -1); }
bool TocItemsRepo::moveTocItemDown(const QString &id) { return moveOneSlot(id, +1); }

bool TocItemsRepo::moveOneSlot(const QString &id, int dir) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    QString docId;
    double cur = 0.0;
    if (!rowDocAndOrder(handle, id, docId, cur)) return false;

    QString neighId;
    double neighOrder = 0.0;
    if (!adjacentNeighbour(handle, docId, cur, dir, neighId, neighOrder)) {

        if (!hasTieAt(handle, docId, cur, id)) return false;
        if (!renormalizeDoc(docId)) return false;
        if (!rowDocAndOrder(handle, id, docId, cur)) return false;
        if (!adjacentNeighbour(handle, docId, cur, dir, neighId, neighOrder))
            return false;
    }

    if (!m_db.beginTransaction()) { logErr(handle, "moveOneSlot begin"); return false; }
    sqlite3_stmt *up = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE toc_items SET order_idx = ? WHERE id = ?;",
            -1, &up, nullptr) != SQLITE_OK) {
        logErr(handle, "moveOneSlot update prepare");
        m_db.rollback();
        return false;
    }
    sqlite3_bind_double(up, 1, neighOrder);
    bindText(up, 2, id);
    bool ok = sqlite3_step(up) == SQLITE_DONE;
    if (ok) {
        sqlite3_reset(up);
        sqlite3_clear_bindings(up);
        sqlite3_bind_double(up, 1, cur);
        bindText(up, 2, neighId);
        ok = sqlite3_step(up) == SQLITE_DONE;
    }
    sqlite3_finalize(up);
    if (!ok) { m_db.rollback(); return false; }
    if (!m_db.commit()) { m_db.rollback(); return false; }
    return true;
}

bool TocItemsRepo::renormalizeDoc(const QString &docId) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    QStringList ids;
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(handle,
                "SELECT id FROM toc_items WHERE doc_id = ? AND deleted_at IS NULL "
                "ORDER BY order_idx, id;",
                -1, &st, nullptr) != SQLITE_OK) {
            logErr(handle, "renormalizeDoc select prepare");
            return false;
        }
        bindText(st, 1, docId);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *t = sqlite3_column_text(st, 0);
            if (t) ids.append(QString::fromUtf8(reinterpret_cast<const char *>(t)));
        }
        sqlite3_finalize(st);
    }
    if (ids.isEmpty()) return true;
    if (!m_db.beginTransaction()) { logErr(handle, "renormalizeDoc begin"); return false; }
    sqlite3_stmt *up = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE toc_items SET order_idx = ? WHERE id = ?;",
            -1, &up, nullptr) != SQLITE_OK) {
        logErr(handle, "renormalizeDoc update prepare");
        m_db.rollback();
        return false;
    }
    for (int i = 0; i < ids.size(); ++i) {
        sqlite3_reset(up);
        sqlite3_clear_bindings(up);
        sqlite3_bind_double(up, 1, (i + 1) * 1000.0);
        bindText(up, 2, ids.at(i));
        if (sqlite3_step(up) != SQLITE_DONE) {
            logErr(handle, "renormalizeDoc update step");
            sqlite3_finalize(up);
            m_db.rollback();
            return false;
        }
    }
    sqlite3_finalize(up);
    if (!m_db.commit()) { m_db.rollback(); return false; }
    return true;
}
