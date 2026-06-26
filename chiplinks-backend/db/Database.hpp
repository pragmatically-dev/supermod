#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct sqlite3;
struct sqlite3_stmt;

class Database {
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path);
    void close();

    int  userVersion();
    bool runInitialMigrationIfNeeded();

    bool migrateToV2();

    bool migrateToV3();

    bool migrateToV4();

    bool migrateToV5();

    bool migrateToV6();

    bool migrateToV7();
    QVariantMap countRows();

    int  purgeSoftDeleted(int days);

    int  resetAll();

    QVariantMap stats(int topN);

    bool incrementalVacuum();

    bool beginTransaction();
    bool commit();
    bool rollback();

    sqlite3 *handle() const { return m_db; }
    QString  lastError() const { return m_lastError; }

private:
    bool exec(const QString &sql);
    bool execScript(const char *sql);

    sqlite3 *m_db = nullptr;
    QString  m_path;
    QString  m_lastError;
};
