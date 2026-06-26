#pragma once

#include <QString>
#include <QVariantList>

class Database;

class RecentsRepo {
public:
    explicit RecentsRepo(Database &db) : m_db(db) {}

    QVariantList top(int limit);

    QVariantList topForDoc(const QString &srcDocId, int limit);

    bool bump(const QString &targetDocId, const QString &targetPageKey,
              const QString &targetPageLabel = QString());

    QVariantList groups(const QString &srcDocId, int limit);

    bool forget(const QString &targetDocId, const QString &targetPageKey);

private:
    Database &m_db;
};
