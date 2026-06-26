#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Database;

class LinksRepo {
public:
    explicit LinksRepo(Database &db) : m_db(db) {}

    QVariantList loadForDoc(const QString &sourceDocId);

    static QString mintId();

    bool upsert(const QVariantMap &link);

    bool remove(const QString &id);

    bool restore(const QString &id);

    bool rename(const QString &id, const QString &name);

    QString sourceDocIdForLink(const QString &id);

    QVariantList search(const QString &query, int limit);

    QVariantList loadAll(bool includeDeleted, int limit);

    QVariantMap graphData(int maxNodes);

private:
    Database &m_db;
};
