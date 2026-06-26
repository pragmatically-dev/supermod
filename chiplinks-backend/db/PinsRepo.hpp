#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Database;

class PinsRepo {
public:
    explicit PinsRepo(Database &db) : m_db(db) {}

    QVariantList loadForLink(const QString &linkId);

    QVariantList loadForPage(const QString &docId, int page);

    QVariantList loadForDoc(const QString &docId);

    bool set(const QString &linkId, int page, qreal x, qreal y, qreal scale, int colorIdx);

    QVariantMap setAutoPlace(const QString &docId, const QString &linkId, int page,
                             qreal defaultX, qreal defaultY, qreal staggerY,
                             qreal collisionRX, qreal collisionRY, qreal maxStaggerY);

    bool setColor(const QString &linkId, int page, int colorIdx);
    bool cycleColor(const QString &linkId, int page);
    bool setPosition(const QString &linkId, int page, qreal x, qreal y,
                     qreal minX, qreal maxX, qreal minY, qreal maxY);
    bool nudge(const QString &linkId, int page, qreal dx, qreal dy,
               qreal minX, qreal maxX, qreal minY, qreal maxY);

    bool remove(const QString &linkId, int page);

private:
    Database &m_db;
};
