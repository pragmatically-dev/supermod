#include "PinsRepo.hpp"
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

QVariantList PinsRepo::loadForLink(const QString &linkId) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT link_id, source_page, x, y, scale, color_idx, metadata,
               created_at, updated_at
        FROM pins
        WHERE link_id = ?
        ORDER BY source_page;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::loadForLink prepare");
        return out;
    }
    bindText(stmt, 1, linkId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PinsRepo::loadForPage(const QString &docId, int page) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT p.link_id, p.source_page, p.x, p.y, p.scale, p.color_idx, p.metadata,
               l.name AS link_name, l.target_doc_id, l.target_page_key
        FROM pins p
        JOIN links l ON l.id = p.link_id AND l.deleted_at IS NULL
        WHERE l.source_doc_id = ? AND p.source_page = ?;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::loadForPage prepare");
        return out;
    }
    bindText(stmt, 1, docId);
    sqlite3_bind_int(stmt, 2, page);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PinsRepo::loadForDoc(const QString &docId) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;

    static const char *kSql = R"sql(
        SELECT p.link_id, p.source_page, p.x, p.y, p.scale, p.color_idx, p.metadata,
               p.created_at, p.updated_at
        FROM pins p
        JOIN links l ON l.id = p.link_id AND l.deleted_at IS NULL
        WHERE l.source_doc_id = ?
        ORDER BY p.link_id, p.source_page;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::loadForDoc prepare");
        return out;
    }
    bindText(stmt, 1, docId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.append(RowMarshaller::toMap(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool PinsRepo::set(const QString &linkId, int page, qreal x, qreal y, qreal scale, int colorIdx) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    static const char *kSql = R"sql(
        INSERT INTO pins (link_id, source_page, x, y, scale, color_idx)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(link_id, source_page) DO UPDATE SET
            x = excluded.x,
            y = excluded.y,
            scale = excluded.scale,
            color_idx = excluded.color_idx;
    )sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::set prepare");
        return false;
    }
    bindText           (stmt, 1, linkId);
    sqlite3_bind_int   (stmt, 2, page);
    sqlite3_bind_double(stmt, 3, x);
    sqlite3_bind_double(stmt, 4, y);
    sqlite3_bind_double(stmt, 5, scale);
    sqlite3_bind_int   (stmt, 6, colorIdx);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PinsRepo::set step");
    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_stmt *up = nullptr;
        if (sqlite3_prepare_v2(handle,
                "UPDATE links SET source_page = ? WHERE id = ? AND deleted_at IS NULL;",
                -1, &up, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(up, 1, page);
            bindText        (up, 2, linkId);
            sqlite3_step(up);
            sqlite3_finalize(up);
        }
    }
    return ok;
}

QVariantMap PinsRepo::setAutoPlace(const QString &docId, const QString &linkId, int page,
                                   qreal defaultX, qreal defaultY, qreal staggerY,
                                   qreal collisionRX, qreal collisionRY, qreal maxStaggerY) {
    QVariantMap result;

    const QVariantList pagePins = loadForPage(docId, page);
    qreal y = defaultY;
    int attempts = 0;
    while (attempts < 12) {
        bool collides = false;
        for (const QVariant &v : pagePins) {
            const QVariantMap p = v.toMap();
            if (qAbs(p.value(QStringLiteral("x")).toDouble() - defaultX) < collisionRX &&
                qAbs(p.value(QStringLiteral("y")).toDouble() - y) < collisionRY) {
                collides = true;
                break;
            }
        }
        if (!collides) break;
        y += staggerY;
        if (y >= maxStaggerY) { y = maxStaggerY; break; }
        attempts++;
    }
    const qreal chosenY = qMin(maxStaggerY, y);
    const bool ok = set(linkId, page, defaultX, chosenY, 1.0, 0);
    result[QStringLiteral("ok")] = ok;
    result[QStringLiteral("x")]  = defaultX;
    result[QStringLiteral("y")]  = chosenY;
    return result;
}

bool PinsRepo::setColor(const QString &linkId, int page, int colorIdx) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    const int c = ((colorIdx % 4) + 4) % 4;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE pins SET color_idx = ? WHERE link_id = ? AND source_page = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::setColor prepare");
        return false;
    }
    sqlite3_bind_int(stmt, 1, c);
    bindText        (stmt, 2, linkId);
    sqlite3_bind_int(stmt, 3, page);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool PinsRepo::cycleColor(const QString &linkId, int page) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE pins SET color_idx = (color_idx + 1) % 4 "
                           "WHERE link_id = ? AND source_page = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::cycleColor prepare");
        return false;
    }
    bindText        (stmt, 1, linkId);
    sqlite3_bind_int(stmt, 2, page);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool PinsRepo::setPosition(const QString &linkId, int page, qreal x, qreal y,
                           qreal minX, qreal maxX, qreal minY, qreal maxY) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    const qreal cx = qBound(minX, x, maxX);
    const qreal cy = qBound(minY, y, maxY);
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE pins SET x = ?, y = ? WHERE link_id = ? AND source_page = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::setPosition prepare");
        return false;
    }
    sqlite3_bind_double(stmt, 1, cx);
    sqlite3_bind_double(stmt, 2, cy);
    bindText           (stmt, 3, linkId);
    sqlite3_bind_int   (stmt, 4, page);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool PinsRepo::nudge(const QString &linkId, int page, qreal dx, qreal dy,
                     qreal minX, qreal maxX, qreal minY, qreal maxY) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE pins SET "
                           "x = MAX(?, MIN(?, x + ?)), "
                           "y = MAX(?, MIN(?, y + ?)) "
                           "WHERE link_id = ? AND source_page = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::nudge prepare");
        return false;
    }
    sqlite3_bind_double(stmt, 1, minX);
    sqlite3_bind_double(stmt, 2, maxX);
    sqlite3_bind_double(stmt, 3, dx);
    sqlite3_bind_double(stmt, 4, minY);
    sqlite3_bind_double(stmt, 5, maxY);
    sqlite3_bind_double(stmt, 6, dy);
    bindText           (stmt, 7, linkId);
    sqlite3_bind_int   (stmt, 8, page);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(handle) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool PinsRepo::remove(const QString &linkId, int page) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "DELETE FROM pins WHERE link_id = ? AND source_page = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PinsRepo::remove prepare");
        return false;
    }
    bindText(stmt, 1, linkId);
    sqlite3_bind_int(stmt, 2, page);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}
