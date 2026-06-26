#pragma once

#include <QString>

class Database;

class MetaRepo {
public:
    explicit MetaRepo(Database &db) : m_db(db) {}

    QString get(const QString &key, const QString &defaultValue = QString());
    bool    set(const QString &key, const QString &value);

private:
    Database &m_db;
};
