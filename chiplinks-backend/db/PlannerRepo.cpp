#include "PlannerRepo.hpp"
#include "Database.hpp"
#include "RowMarshaller.hpp"

#include <QHash>
#include <QStringList>
#include <sqlite3.h>
#include <cstdio>

namespace {

bool bindText(sqlite3_stmt *stmt, int idx, const QString &s) {
    if (s.isNull()) return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    const QByteArray utf8 = s.toUtf8();
    return sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindTextOrNull(sqlite3_stmt *stmt, int idx, const QVariant &v) {
    const QString s = v.toString();
    if (!v.isValid() || s.isEmpty()) return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    return bindText(stmt, idx, s);
}

bool bindIntOrNull(sqlite3_stmt *stmt, int idx, const QVariant &v) {
    if (!v.isValid() || v.toString().isEmpty())
        return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
    return sqlite3_bind_int64(stmt, idx, v.toLongLong()) == SQLITE_OK;
}

void logErr(sqlite3 *db, const char *op) {
    std::fprintf(stderr, "[chiplinks-backend] %s failed: %s\n", op,
                 db ? sqlite3_errmsg(db) : "(no db)");
}

QString textCol(sqlite3_stmt *stmt, int col) {
    const unsigned char *t = sqlite3_column_text(stmt, col);
    return t ? QString::fromUtf8(reinterpret_cast<const char *>(t)) : QString();
}

const char *const kEventCols =
    "id, ymd, start_min, end_min, all_day, title, notes, color_idx, remind_min, "
    "metadata, created_at, updated_at";
const char *const kTaskCols =
    "id, due_ymd, list, title, done, priority, order_idx, remind_at, "
    "metadata, created_at, updated_at";
const char *const kEntryCols =
    "id, kind, bucket, title, body, num, done, order_idx, metadata, created_at, updated_at";

}

QVariantList PlannerRepo::loadEventsForDay(const QString &ymd) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    const QString sql = QStringLiteral(
        "SELECT %1 FROM planner_events WHERE ymd = ? AND deleted_at IS NULL "
        "ORDER BY all_day DESC, start_min, created_at;").arg(QLatin1String(kEventCols));
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadEventsForDay prepare");
        return out;
    }
    bindText(stmt, 1, ymd);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PlannerRepo::loadMonthCells(int year, int month) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    const QString first = QStringLiteral("%1-%2-01")
        .arg(year, 4, 10, QChar('0')).arg(month, 2, 10, QChar('0'));
    const QString last = QStringLiteral("%1-%2-31")
        .arg(year, 4, 10, QChar('0')).arg(month, 2, 10, QChar('0'));

    QHash<QString, QVariantMap> byDay;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT ymd, COUNT(*) FROM planner_events "
            "WHERE deleted_at IS NULL AND ymd >= ? AND ymd <= ? GROUP BY ymd;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        bindText(stmt, 1, first);
        bindText(stmt, 2, last);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const QString d = textCol(stmt, 0);
            QVariantMap &m = byDay[d];
            m[QStringLiteral("ymd")] = d;
            m[QStringLiteral("events")] = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    } else {
        logErr(handle, "PlannerRepo::loadMonthCells events prepare");
    }

    stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT due_ymd, COUNT(*), SUM(done) FROM planner_tasks "
            "WHERE deleted_at IS NULL AND due_ymd >= ? AND due_ymd <= ? GROUP BY due_ymd;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        bindText(stmt, 1, first);
        bindText(stmt, 2, last);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const QString d = textCol(stmt, 0);
            QVariantMap &m = byDay[d];
            m[QStringLiteral("ymd")] = d;
            m[QStringLiteral("tasks")] = sqlite3_column_int(stmt, 1);
            m[QStringLiteral("tasksDone")] = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
    } else {
        logErr(handle, "PlannerRepo::loadMonthCells tasks prepare");
    }

    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) out.append(it.value());
    return out;
}

bool PlannerRepo::upsertEvent(const QVariantMap &e) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO planner_events (id, ymd, start_min, end_min, all_day, title,
                                    notes, color_idx, remind_min)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            ymd        = excluded.ymd,
            start_min  = excluded.start_min,
            end_min    = excluded.end_min,
            all_day    = excluded.all_day,
            title      = excluded.title,
            notes      = excluded.notes,
            color_idx  = excluded.color_idx,
            remind_min = excluded.remind_min,
            deleted_at = NULL;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::upsertEvent prepare");
        return false;
    }
    bindText        (stmt, 1, e.value(QStringLiteral("id")).toString());
    bindText        (stmt, 2, e.value(QStringLiteral("ymd")).toString());
    sqlite3_bind_int(stmt, 3, e.value(QStringLiteral("start_min")).toInt());
    sqlite3_bind_int(stmt, 4, e.value(QStringLiteral("end_min")).toInt());
    sqlite3_bind_int(stmt, 5, e.value(QStringLiteral("all_day")).toInt());
    bindText        (stmt, 6, e.value(QStringLiteral("title")).toString());
    bindText        (stmt, 7, e.value(QStringLiteral("notes")).toString());
    sqlite3_bind_int(stmt, 8, e.value(QStringLiteral("color_idx")).toInt());
    sqlite3_bind_int(stmt, 9, e.value(QStringLiteral("remind_min"), -1).toInt());
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::upsertEvent step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::deleteEvent(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_events SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::deleteEvent prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QString PlannerRepo::ymdForEvent(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return QString();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT ymd FROM planner_events WHERE id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::ymdForEvent prepare");
        return QString();
    }
    bindText(stmt, 1, id);
    QString out;
    if (sqlite3_step(stmt) == SQLITE_ROW) out = textCol(stmt, 0);
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PlannerRepo::loadTasks(const QString &list, const QString &ymd, bool includeDone) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    QString sql = QStringLiteral("SELECT %1 FROM planner_tasks WHERE deleted_at IS NULL")
                      .arg(QLatin1String(kTaskCols));
    if (!list.isEmpty())  sql += QStringLiteral(" AND list = ?");
    if (!ymd.isEmpty())   sql += QStringLiteral(" AND due_ymd = ?");
    if (!includeDone)     sql += QStringLiteral(" AND done = 0");
    sql += QStringLiteral(" ORDER BY done, order_idx, created_at;");
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadTasks prepare");
        return out;
    }
    int idx = 1;
    if (!list.isEmpty()) bindText(stmt, idx++, list);
    if (!ymd.isEmpty())  bindText(stmt, idx++, ymd);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool PlannerRepo::upsertTask(const QVariantMap &t) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO planner_tasks (id, due_ymd, list, title, done, priority, order_idx, remind_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            due_ymd   = excluded.due_ymd,
            list      = excluded.list,
            title     = excluded.title,
            done      = excluded.done,
            priority  = excluded.priority,
            order_idx = excluded.order_idx,
            remind_at = excluded.remind_at,
            deleted_at = NULL;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::upsertTask prepare");
        return false;
    }
    bindText           (stmt, 1, t.value(QStringLiteral("id")).toString());
    bindTextOrNull     (stmt, 2, t.value(QStringLiteral("due_ymd")));
    bindText           (stmt, 3, t.value(QStringLiteral("list"), QStringLiteral("todo")).toString());
    bindText           (stmt, 4, t.value(QStringLiteral("title")).toString());
    sqlite3_bind_int   (stmt, 5, t.value(QStringLiteral("done")).toInt());
    sqlite3_bind_int   (stmt, 6, t.value(QStringLiteral("priority")).toInt());
    sqlite3_bind_double(stmt, 7, t.value(QStringLiteral("order_idx")).toDouble());
    bindIntOrNull      (stmt, 8, t.value(QStringLiteral("remind_at")));
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::upsertTask step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::toggleTask(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_tasks SET done = 1 - done WHERE id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::toggleTask prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::deleteTask(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_tasks SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::deleteTask prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList PlannerRepo::loadHabits() {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT id, name, order_idx, archived FROM planner_habits "
            "WHERE deleted_at IS NULL ORDER BY order_idx, created_at;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadHabits prepare");
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PlannerRepo::loadHabitMarks(const QString &monthPrefix) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT habit_id, ymd, value FROM planner_habit_marks WHERE ymd LIKE ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadHabitMarks prepare");
        return out;
    }
    bindText(stmt, 1, monthPrefix + QStringLiteral("%"));
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool PlannerRepo::upsertHabit(const QVariantMap &h) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO planner_habits (id, name, order_idx, archived)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            name      = excluded.name,
            order_idx = excluded.order_idx,
            archived  = excluded.archived,
            deleted_at = NULL;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::upsertHabit prepare");
        return false;
    }
    bindText           (stmt, 1, h.value(QStringLiteral("id")).toString());
    bindText           (stmt, 2, h.value(QStringLiteral("name")).toString());
    sqlite3_bind_double(stmt, 3, h.value(QStringLiteral("order_idx")).toDouble());
    sqlite3_bind_int   (stmt, 4, h.value(QStringLiteral("archived")).toInt());
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::upsertHabit step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::setHabitMark(const QString &habitId, const QString &ymd, int value) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    const char *kSql = (value <= 0)
        ? "DELETE FROM planner_habit_marks WHERE habit_id = ? AND ymd = ?;"
        : "INSERT INTO planner_habit_marks (habit_id, ymd, value) VALUES (?, ?, ?) "
          "ON CONFLICT(habit_id, ymd) DO UPDATE SET value = excluded.value;";
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::setHabitMark prepare");
        return false;
    }
    bindText(stmt, 1, habitId);
    bindText(stmt, 2, ymd);
    if (value > 0) sqlite3_bind_int(stmt, 3, value);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantMap PlannerRepo::inkBinding(const QString &pageKey) {
    QVariantMap out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT page_key, scratch_doc_id, scratch_page FROM planner_ink WHERE page_key = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::inkBinding prepare");
        return out;
    }
    bindText(stmt, 1, pageKey);
    if (sqlite3_step(stmt) == SQLITE_ROW) out = RowMarshaller::toMap(stmt);
    sqlite3_finalize(stmt);
    return out;
}

QVariantList PlannerRepo::inkBindingsLike(const QString &prefix) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT page_key, scratch_doc_id, scratch_page FROM planner_ink WHERE page_key LIKE ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::inkBindingsLike prepare");
        return out;
    }

    bindText(stmt, 1, prefix + QStringLiteral("%"));
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool PlannerRepo::setInkBinding(const QString &pageKey, const QString &scratchDocId,
                                const QString &scratchPage) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql =
        "INSERT INTO planner_ink (page_key, scratch_doc_id, scratch_page) VALUES (?, ?, ?) "
        "ON CONFLICT(page_key) DO UPDATE SET "
        "scratch_doc_id = excluded.scratch_doc_id, scratch_page = excluded.scratch_page;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::setInkBinding prepare");
        return false;
    }
    bindText(stmt, 1, pageKey);
    bindText(stmt, 2, scratchDocId);
    bindText(stmt, 3, scratchPage);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::setInkBinding step");
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::deleteInkBinding(const QString &pageKey) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "DELETE FROM planner_ink WHERE page_key = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::deleteInkBinding prepare");
        return false;
    }
    bindText(stmt, 1, pageKey);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::deleteInkBinding step");
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList PlannerRepo::loadEntries(const QString &kind, const QString &bucket) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    QString sql = QStringLiteral("SELECT %1 FROM planner_entries WHERE deleted_at IS NULL AND kind = ?")
                      .arg(QLatin1String(kEntryCols));
    if (!bucket.isEmpty()) sql += QStringLiteral(" AND bucket = ?");
    sql += QStringLiteral(" ORDER BY order_idx, created_at;");
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadEntries prepare");
        return out;
    }
    bindText(stmt, 1, kind);
    if (!bucket.isEmpty()) bindText(stmt, 2, bucket);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.append(RowMarshaller::toMap(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool PlannerRepo::upsertEntry(const QVariantMap &e) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO planner_entries (id, kind, bucket, title, body, num, done, order_idx)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            kind      = excluded.kind,
            bucket    = excluded.bucket,
            title     = excluded.title,
            body      = excluded.body,
            num       = excluded.num,
            done      = excluded.done,
            order_idx = excluded.order_idx,
            deleted_at = NULL;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::upsertEntry prepare");
        return false;
    }
    bindText           (stmt, 1, e.value(QStringLiteral("id")).toString());
    bindText           (stmt, 2, e.value(QStringLiteral("kind")).toString());
    bindText           (stmt, 3, e.value(QStringLiteral("bucket")).toString());
    bindText           (stmt, 4, e.value(QStringLiteral("title")).toString());
    bindText           (stmt, 5, e.value(QStringLiteral("body")).toString());
    if (e.value(QStringLiteral("num")).isValid() && !e.value(QStringLiteral("num")).toString().isEmpty())
        sqlite3_bind_double(stmt, 6, e.value(QStringLiteral("num")).toDouble());
    else
        sqlite3_bind_null(stmt, 6);
    sqlite3_bind_int   (stmt, 7, e.value(QStringLiteral("done")).toInt());
    sqlite3_bind_double(stmt, 8, e.value(QStringLiteral("order_idx")).toDouble());
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "PlannerRepo::upsertEntry step");
    sqlite3_finalize(stmt);
    return ok;
}

QVariantMap PlannerRepo::entryById(const QString &id) {
    QVariantMap out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT kind, bucket FROM planner_entries WHERE id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::entryById prepare");
        return out;
    }
    bindText(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.insert(QStringLiteral("kind"),
                   QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))));
        out.insert(QStringLiteral("bucket"),
                   QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1))));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool PlannerRepo::deleteEntry(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_entries SET deleted_at = unixepoch() WHERE id = ? AND deleted_at IS NULL;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::deleteEntry prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList PlannerRepo::loadUpcoming(qint64 nowEpoch, int withinSec) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    const qint64 until = nowEpoch + (withinSec > 0 ? withinSec : 0);

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,

            "SELECT id, title, ymd, "
            "(CAST(strftime('%s', ymd, 'utc') AS INTEGER) + start_min*60) AS when_epoch, "
            "CASE WHEN remind_min >= 0 "
            "     THEN (CAST(strftime('%s', ymd, 'utc') AS INTEGER) + start_min*60 - remind_min*60) "
            "     ELSE NULL END AS remind_epoch "
            "FROM planner_events "
            "WHERE deleted_at IS NULL AND all_day = 0 "
            "AND (CAST(strftime('%s', ymd, 'utc') AS INTEGER) + start_min*60) BETWEEN ? AND ? "
            "ORDER BY when_epoch;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nowEpoch);
        sqlite3_bind_int64(stmt, 2, until);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QVariantMap m;
            m[QStringLiteral("id")] = textCol(stmt, 0);
            m[QStringLiteral("kind")] = QStringLiteral("event");
            m[QStringLiteral("title")] = textCol(stmt, 1);
            m[QStringLiteral("ymd")] = textCol(stmt, 2);
            m[QStringLiteral("when_epoch")] = static_cast<qlonglong>(sqlite3_column_int64(stmt, 3));
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                m[QStringLiteral("remind_epoch")] = static_cast<qlonglong>(sqlite3_column_int64(stmt, 4));
            out.append(m);
        }
        sqlite3_finalize(stmt);
    } else {
        logErr(handle, "PlannerRepo::loadUpcoming events prepare");
    }

    stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT id, title, due_ymd, remind_at FROM planner_tasks "
            "WHERE deleted_at IS NULL AND done = 0 AND remind_at IS NOT NULL "
            "AND remind_at BETWEEN ? AND ? ORDER BY remind_at;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nowEpoch);
        sqlite3_bind_int64(stmt, 2, until);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QVariantMap m;
            m[QStringLiteral("id")] = textCol(stmt, 0);
            m[QStringLiteral("kind")] = QStringLiteral("task");
            m[QStringLiteral("title")] = textCol(stmt, 1);
            m[QStringLiteral("ymd")] = textCol(stmt, 2);
            m[QStringLiteral("when_epoch")] = static_cast<qlonglong>(sqlite3_column_int64(stmt, 3));

            m[QStringLiteral("remind_epoch")] = static_cast<qlonglong>(sqlite3_column_int64(stmt, 3));
            out.append(m);
        }
        sqlite3_finalize(stmt);
    } else {
        logErr(handle, "PlannerRepo::loadUpcoming tasks prepare");
    }

    return out;
}

bool PlannerRepo::logNotification(const QVariantMap &n) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql =
        "INSERT OR IGNORE INTO planner_notifications "
        "(id, kind, ref_id, title, ymd, mins, fired_epoch, read, snooze_epoch) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, NULL);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::logNotification prepare");
        return false;
    }
    bindText(stmt, 1, n.value(QStringLiteral("id")).toString());
    bindText(stmt, 2, n.value(QStringLiteral("kind")).toString());
    bindText(stmt, 3, n.value(QStringLiteral("ref_id")).toString());
    bindText(stmt, 4, n.value(QStringLiteral("title")).toString());
    bindText(stmt, 5, n.value(QStringLiteral("ymd")).toString());
    sqlite3_bind_int  (stmt, 6, n.value(QStringLiteral("mins")).toInt());
    sqlite3_bind_int64(stmt, 7, n.value(QStringLiteral("fired_epoch")).toLongLong());
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList PlannerRepo::loadNotifications(int limit) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT id, kind, ref_id, title, ymd, mins, fired_epoch, read "
            "FROM planner_notifications ORDER BY fired_epoch DESC LIMIT ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::loadNotifications prepare");
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 100);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m[QStringLiteral("id")]          = textCol(stmt, 0);
        m[QStringLiteral("kind")]        = textCol(stmt, 1);
        m[QStringLiteral("ref_id")]      = textCol(stmt, 2);
        m[QStringLiteral("title")]       = textCol(stmt, 3);
        m[QStringLiteral("ymd")]         = textCol(stmt, 4);
        m[QStringLiteral("mins")]        = sqlite3_column_int(stmt, 5);
        m[QStringLiteral("fired_epoch")] = static_cast<qlonglong>(sqlite3_column_int64(stmt, 6));
        m[QStringLiteral("read")]        = sqlite3_column_int(stmt, 7);
        out.append(m);
    }
    sqlite3_finalize(stmt);
    return out;
}

int PlannerRepo::unreadNotificationCount() {
    sqlite3 *handle = m_db.handle();
    if (!handle) return 0;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT COUNT(*) FROM planner_notifications WHERE read = 0;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::unreadNotificationCount prepare");
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

bool PlannerRepo::markNotificationRead(const QString &id) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_notifications SET read = 1 WHERE id = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::markNotificationRead prepare");
        return false;
    }
    bindText(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool PlannerRepo::markAllNotificationsRead() {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    const bool ok = sqlite3_exec(handle,
        "UPDATE planner_notifications SET read = 1 WHERE read = 0;",
        nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) logErr(handle, "PlannerRepo::markAllNotificationsRead");
    return ok;
}

bool PlannerRepo::clearNotifications() {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    const bool ok = sqlite3_exec(handle, "DELETE FROM planner_notifications;",
                                 nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) logErr(handle, "PlannerRepo::clearNotifications");
    return ok;
}

bool PlannerRepo::snoozeNotification(const QString &id, qint64 untilEpoch) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "UPDATE planner_notifications SET snooze_epoch = ?, read = 1 WHERE id = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::snoozeNotification prepare");
        return false;
    }
    sqlite3_bind_int64(stmt, 1, untilEpoch);
    bindText(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVariantList PlannerRepo::takeDueSnoozed(qint64 nowEpoch) {
    QVariantList out;
    sqlite3 *handle = m_db.handle();
    if (!handle) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
            "SELECT id, kind, ref_id, title, ymd, mins FROM planner_notifications "
            "WHERE snooze_epoch IS NOT NULL AND snooze_epoch <= ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "PlannerRepo::takeDueSnoozed prepare");
        return out;
    }
    sqlite3_bind_int64(stmt, 1, nowEpoch);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m[QStringLiteral("id")]     = textCol(stmt, 0);
        m[QStringLiteral("kind")]   = textCol(stmt, 1);
        m[QStringLiteral("ref_id")] = textCol(stmt, 2);
        m[QStringLiteral("title")]  = textCol(stmt, 3);
        m[QStringLiteral("ymd")]    = textCol(stmt, 4);
        m[QStringLiteral("mins")]   = sqlite3_column_int(stmt, 5);
        out.append(m);
    }
    sqlite3_finalize(stmt);

    if (!out.isEmpty()) {
        sqlite3_stmt *upd = nullptr;
        if (sqlite3_prepare_v2(handle,
                "UPDATE planner_notifications SET snooze_epoch = NULL, read = 0 "
                "WHERE snooze_epoch IS NOT NULL AND snooze_epoch <= ?;",
                -1, &upd, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(upd, 1, nowEpoch);
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
    }
    return out;
}

QVariantMap PlannerRepo::stats() {
    QVariantMap m;
    sqlite3 *handle = m_db.handle();
    if (!handle) return m;
    auto cnt = [&](const char *sql) -> int {
        sqlite3_stmt *s = nullptr; int v = 0;
        if (sqlite3_prepare_v2(handle, sql, -1, &s, nullptr) == SQLITE_OK) {
            if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
            sqlite3_finalize(s);
        }
        return v;
    };
    m[QStringLiteral("events")]      = cnt("SELECT COUNT(*) FROM planner_events WHERE deleted_at IS NULL;");
    m[QStringLiteral("tasks")]       = cnt("SELECT COUNT(*) FROM planner_tasks WHERE deleted_at IS NULL;");
    m[QStringLiteral("tasksDone")]   = cnt("SELECT COUNT(*) FROM planner_tasks WHERE deleted_at IS NULL AND done = 1;");
    m[QStringLiteral("habits")]      = cnt("SELECT COUNT(*) FROM planner_habits WHERE deleted_at IS NULL AND archived = 0;");
    m[QStringLiteral("habitMarks")]  = cnt("SELECT COUNT(*) FROM planner_habit_marks;");
    m[QStringLiteral("goals")]       = cnt("SELECT COUNT(*) FROM planner_entries WHERE deleted_at IS NULL AND kind = 'goal';");
    m[QStringLiteral("collections")] = cnt("SELECT COUNT(*) FROM planner_entries WHERE deleted_at IS NULL AND kind = 'collection';");
    m[QStringLiteral("budget")]      = cnt("SELECT COUNT(*) FROM planner_entries WHERE deleted_at IS NULL AND kind = 'budget';");
    m[QStringLiteral("moods")]       = cnt("SELECT COUNT(*) FROM planner_entries WHERE deleted_at IS NULL AND kind = 'mood';");
    m[QStringLiteral("ink")]         = cnt("SELECT COUNT(*) FROM planner_ink;");
    m[QStringLiteral("notifications")] = cnt("SELECT COUNT(*) FROM planner_notifications;");
    return m;
}

bool PlannerRepo::reset(const QString &scope) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    QStringList sqls;
    if (scope == QLatin1String("events")  || scope == QLatin1String("all")) sqls << QStringLiteral("DELETE FROM planner_events;");
    if (scope == QLatin1String("tasks")   || scope == QLatin1String("all")) sqls << QStringLiteral("DELETE FROM planner_tasks;");
    if (scope == QLatin1String("habits")  || scope == QLatin1String("all")) { sqls << QStringLiteral("DELETE FROM planner_habit_marks;") << QStringLiteral("DELETE FROM planner_habits;"); }
    if (scope == QLatin1String("entries") || scope == QLatin1String("all")) sqls << QStringLiteral("DELETE FROM planner_entries;");
    if (scope == QLatin1String("ink")     || scope == QLatin1String("all")) sqls << QStringLiteral("DELETE FROM planner_ink;");
    if (scope == QLatin1String("notifications") || scope == QLatin1String("all")) sqls << QStringLiteral("DELETE FROM planner_notifications;");
    bool ok = true;
    for (const QString &s : sqls) {
        char *err = nullptr;
        if (sqlite3_exec(handle, s.toUtf8().constData(), nullptr, nullptr, &err) != SQLITE_OK) {
            ok = false;
            if (err) { logErr(handle, "PlannerRepo::reset"); sqlite3_free(err); }
        }
    }
    return ok;
}
