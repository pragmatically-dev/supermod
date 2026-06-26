#pragma once

#include <QString>
#include <QVariantList>

class Database;

class DocsRepo {
public:
    explicit DocsRepo(Database &db) : m_db(db) {}

    QVariantList loadAll(bool includeDeleted, int limit);

    bool upsert(const QString &docId, const QString &name, const QString &parentId);

    bool markDeleted(const QString &docId);

    bool restore(const QString &docId);

private:
    Database &m_db;
};
