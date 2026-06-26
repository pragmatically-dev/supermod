#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Database;

class PlannerRepo {
public:
    explicit PlannerRepo(Database &db) : m_db(db) {}

    QVariantList loadEventsForDay(const QString &ymd);

    QVariantList loadMonthCells(int year, int month);
    bool         upsertEvent(const QVariantMap &e);
    bool         deleteEvent(const QString &id);
    QString      ymdForEvent(const QString &id);

    QVariantList loadTasks(const QString &list, const QString &ymd, bool includeDone);
    bool         upsertTask(const QVariantMap &t);
    bool         toggleTask(const QString &id);
    bool         deleteTask(const QString &id);

    QVariantList loadHabits();

    QVariantList loadHabitMarks(const QString &monthPrefix);
    bool         upsertHabit(const QVariantMap &h);

    bool         setHabitMark(const QString &habitId, const QString &ymd, int value);

    QVariantMap  inkBinding(const QString &pageKey);
    QVariantList inkBindingsLike(const QString &prefix);
    bool         setInkBinding(const QString &pageKey, const QString &scratchDocId,
                               const QString &scratchPage);
    bool         deleteInkBinding(const QString &pageKey);

    QVariantList loadEntries(const QString &kind, const QString &bucket);
    bool         upsertEntry(const QVariantMap &e);
    bool         deleteEntry(const QString &id);
    QVariantMap  entryById(const QString &id);

    QVariantList loadUpcoming(qint64 nowEpoch, int withinSec);

    bool         logNotification(const QVariantMap &n);
    QVariantList loadNotifications(int limit);
    int          unreadNotificationCount();
    bool         markNotificationRead(const QString &id);
    bool         markAllNotificationsRead();
    bool         clearNotifications();
    bool         snoozeNotification(const QString &id, qint64 untilEpoch);

    QVariantList takeDueSnoozed(qint64 nowEpoch);

    QVariantMap stats();

    bool        reset(const QString &scope);

private:
    Database &m_db;
};
