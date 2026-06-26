#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class PlannerRepo;

class NotificationService : public QObject {
    Q_OBJECT
public:
    explicit NotificationService(PlannerRepo *planner, QObject *parent = nullptr);

    void start(int intervalSec, int windowSec);
    void stop();
    QVariantList current() const { return m_current; }

public slots:
    void poll();

signals:
    void upcomingChanged(const QVariantList &items);
    void due(const QVariantMap &item);

private:
    PlannerRepo  *m_planner = nullptr;
    QTimer        m_timer;
    int           m_windowSec   = 3600;
    int           m_intervalSec = 60;
    QSet<QString> m_seen;
    QVariantList  m_current;
};
