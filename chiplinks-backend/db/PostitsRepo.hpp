#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

class PostitsRepo {
public:
    explicit PostitsRepo(Database &db) : m_db(db) {}

    QVariantList loadForPage(const QString &hostDocId, int page);

    int countForScratchPages(const QString &scratchDocId, const QStringList &pageKeys);

    QVariantList loadAll(bool includeDeleted, int limit);

    bool upsert(const QVariantMap &p);

    bool setGeometry(const QString &id, qreal x, qreal y, qreal scale, int colorIdx);
    bool setTitle(const QString &id, const QString &title);

    bool markDeleted(const QString &id);
    bool restore(const QString &id);

    QString hostDocIdForPostit(const QString &id);

private:
    Database &m_db;
};
