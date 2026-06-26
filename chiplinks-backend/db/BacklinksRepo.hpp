#pragma once

#include <QString>
#include <QVariantList>

class Database;

class BacklinksRepo {
public:
    explicit BacklinksRepo(Database &db) : m_db(db) {}

    QVariantList query(const QString &targetDocId, const QString &targetPageKey);

    QVariantList groupsForDoc(const QString &targetDocId, const QString &targetPageKey);

    int count(const QString &targetDocId, const QString &targetPageKey);

private:
    Database &m_db;
};
