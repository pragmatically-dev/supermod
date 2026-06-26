#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

class TocItemsRepo {
public:
    explicit TocItemsRepo(Database &db) : m_db(db) {}

    QVariantList loadForDoc(const QString &docId);

    QVariantList loadTocCustom(const QString &docId);

    QVariantMap docXrefGraph(const QString &docId);

    bool upsert(const QVariantMap &item);

    bool remove(const QString &id);

    bool rename(const QString &id, const QString &name);

    bool setLevel(const QString &id, int level);

    bool restore(const QString &id);

    int removeMany(const QStringList &ids);

    QString docIdForItem(const QString &id);

    bool reorder(const QString &id, const QString &newParentId, double newOrderIdx);

    bool moveTocItemUp(const QString &id);
    bool moveTocItemDown(const QString &id);

    int copyForDuplicate(const QString &sourceDocId, const QString &newDocId);

private:

    double nextOrderIdxForDoc(const QString &docId);

    bool   moveOneSlot(const QString &id, int dir);

    bool   renormalizeDoc(const QString &docId);

    Database &m_db;
};
