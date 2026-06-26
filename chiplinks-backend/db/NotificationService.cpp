#include "NotificationService.hpp"
#include "PlannerRepo.hpp"

#include <QDateTime>

NotificationService::NotificationService(PlannerRepo *planner, QObject *parent)
    : QObject(parent), m_planner(planner) {
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &NotificationService::poll);
}

void NotificationService::start(int intervalSec, int windowSec) {
    m_windowSec   = windowSec > 0 ? windowSec : 3600;
    m_intervalSec = intervalSec > 0 ? intervalSec : 60;
    m_timer.start(m_intervalSec * 1000);
    poll();
}

void NotificationService::stop() {
    m_timer.stop();
}

void NotificationService::poll() {
    if (!m_planner) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QVariantList items = m_planner->loadUpcoming(now, m_windowSec);

    const qint64 grace = 2 * static_cast<qint64>(m_intervalSec);
    static const int kThresholds[3] = { 30, 15, 5 };
    QSet<QString> nowBases;

    auto logFired = [&](const QString &cid, const QVariantMap &out) {
        QVariantMap n;
        n[QStringLiteral("id")]          = cid;
        n[QStringLiteral("kind")]        = out.value(QStringLiteral("kind"));
        n[QStringLiteral("ref_id")]      = out.value(QStringLiteral("id"));
        n[QStringLiteral("title")]       = out.value(QStringLiteral("title"));
        n[QStringLiteral("ymd")]         = out.value(QStringLiteral("ymd"));
        n[QStringLiteral("mins")]        = out.value(QStringLiteral("mins"));
        n[QStringLiteral("fired_epoch")] = static_cast<qlonglong>(now);
        m_planner->logNotification(n);
    };

    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const QString kind = m.value(QStringLiteral("kind")).toString();
        const QString base = kind + QStringLiteral(":") + m.value(QStringLiteral("id")).toString();
        nowBases.insert(base);

        if (kind == QLatin1String("event")) {
            const qint64 start = m.value(QStringLiteral("when_epoch")).toLongLong();
            for (int i = 0; i < 3; ++i) {
                const qint64 fireAt = start - static_cast<qint64>(kThresholds[i]) * 60;
                const QString cid = base + QStringLiteral(":") + QString::number(kThresholds[i]);
                if (fireAt <= now && fireAt > now - grace && !m_seen.contains(cid)) {
                    m_seen.insert(cid);
                    QVariantMap out = m;
                    out[QStringLiteral("mins")] = kThresholds[i];
                    logFired(cid, out);
                    emit due(out);
                }
            }
        } else {
            const QVariant re = m.value(QStringLiteral("remind_epoch"));
            if (!re.isValid() || re.isNull()) continue;
            const qint64 remindAt = re.toLongLong();
            const QString cid = base + QStringLiteral(":t");
            if (remindAt <= now && remindAt > now - grace && !m_seen.contains(cid)) {
                m_seen.insert(cid);
                QVariantMap out = m;
                out[QStringLiteral("mins")] = static_cast<int>((m.value(QStringLiteral("when_epoch")).toLongLong() - now) / 60);
                logFired(cid, out);
                emit due(out);
            }
        }
    }

    QSet<QString> keep;
    for (const QString &s : m_seen) {
        const int idx = s.lastIndexOf(QLatin1Char(':'));
        const QString b = idx >= 0 ? s.left(idx) : s;
        if (nowBases.contains(b)) keep.insert(s);
    }
    m_seen = keep;

    const QVariantList snoozed = m_planner->takeDueSnoozed(now);
    for (const QVariant &v : snoozed) {
        QVariantMap out = v.toMap();
        out[QStringLiteral("mins")] = 0;
        emit due(out);
    }

    m_current = items;
    emit upcomingChanged(items);
}
