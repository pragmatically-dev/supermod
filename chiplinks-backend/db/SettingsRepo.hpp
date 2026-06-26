#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

class Database;

class SettingsRepo {
public:
    struct Def {
        const char *key;
        const char *section;
        const char *type;
        const char *label;
        const char *desc;
        const char *def;
        double      min, max, step;
        int         dec;
        const char *choices;
    };

    explicit SettingsRepo(Database &db) : m_db(db) {}

    QVariantList schemaFor(const QString &section);

    QVariantMap  loadSection(const QString &section);

    QString      setClamped(const QString &key, const QString &value);

    QString      resetSection(const QString &section);

    static const std::vector<Def> &schema();
    static const Def *defFor(const QString &key);
    static QStringList choiceList(const Def *d);

private:
    QString rawGet(const QString &key);
    bool    rawSet(const QString &key, const QString &section, const QString &value);

    Database &m_db;
};
