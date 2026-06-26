#include "ChiplinksBackend.hpp"

#include "db/BackendTrace.hpp"
#include "db/Database.hpp"
#include "db/DocsRepo.hpp"
#include "db/TocItemsRepo.hpp"
#include "db/LinksRepo.hpp"
#include "db/PinsRepo.hpp"
#include "db/BacklinksRepo.hpp"
#include "db/RecentsRepo.hpp"
#include "db/MetaRepo.hpp"
#include "db/PostitsRepo.hpp"
#include "db/SettingsRepo.hpp"
#include "db/PlannerRepo.hpp"
#include "db/NotificationService.hpp"
#include "db/GraphLayout.hpp"
#include "db/TocNumbering.hpp"
#include "db/LinkFlow.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>
#include <cstdio>

namespace {

QString defaultDbPath() {
    return QStringLiteral("/home/root/.local/share/remarkable/xochitl/chiplinks.db");
}

}

ChiplinksBackend::ChiplinksBackend(QObject *parent)
    : QObject(parent), m_db(std::make_unique<Database>()) {
    std::fprintf(stderr, "[chiplinks-backend] ChiplinksBackend constructed\n");

    m_linkFlow = std::make_unique<LinkFlow>();

    std::fprintf(stderr, "[chiplinks-trace] middleware level=%d (0=off 1=cmd+sig 2=+reads; env CHIPLINKS_TRACE / setTracing)\n",
                 BackendTrace::level());
#define TRACE_SIG1(sig) \
    connect(this, &ChiplinksBackend::sig, this, [](const QString &a){ \
        if (BackendTrace::on(1)) BackendTrace::out(#sig, BackendTrace::brief(a)); })
#define TRACE_SIGMAP(sig) \
    connect(this, &ChiplinksBackend::sig, this, [](const QVariantMap &a){ \
        if (BackendTrace::on(1)) BackendTrace::out(#sig, BackendTrace::brief(a)); })
    TRACE_SIG1(docsChanged);
    TRACE_SIG1(tocItemsChanged);
    TRACE_SIG1(linksChanged);
    TRACE_SIG1(pinsChanged);
    TRACE_SIG1(postitsChanged);
    TRACE_SIG1(metaChanged);
    TRACE_SIG1(settingsChanged);
    TRACE_SIGMAP(statsReady);
    TRACE_SIGMAP(graphDataReady);
    TRACE_SIGMAP(graphLayoutReady);
    connect(this, &ChiplinksBackend::recentsChanged, this, []{
        if (BackendTrace::on(1)) BackendTrace::out("recentsChanged", QString()); });
    connect(this, &ChiplinksBackend::backlinksReady, this,
            [](const QString &d, const QString &k, const QVariantList &l){
        if (BackendTrace::on(1)) BackendTrace::out("backlinksReady",
            QStringLiteral("%1:%2 [%3]").arg(d, k).arg(l.size())); });
    connect(this, &ChiplinksBackend::backlinkGroupsReady, this,
            [](const QString &d, const QString &k, const QVariantList &g){
        if (BackendTrace::on(1)) BackendTrace::out("backlinkGroupsReady",
            QStringLiteral("%1:%2 groups=%3").arg(d, k).arg(g.size())); });
    connect(this, &ChiplinksBackend::maintenanceDone, this, [](const QString &op, int n){
        if (BackendTrace::on(1)) BackendTrace::out("maintenanceDone",
            QStringLiteral("%1 n=%2").arg(op).arg(n)); });
    connect(this, &ChiplinksBackend::error, this, [](const QString &op, const QString &m){
        if (BackendTrace::on(1)) BackendTrace::out("error", QStringLiteral("%1: %2").arg(op, m)); });
    connect(this, &ChiplinksBackend::docGraphReady, this, [](const QString &d, const QVariantMap &s){
        if (BackendTrace::on(1)) BackendTrace::out("docGraphReady", d + QStringLiteral(" ") + BackendTrace::brief(s)); });
    connect(this, &ChiplinksBackend::linkFlowAction, this, [](const QString &act, const QVariantMap &p){
        if (BackendTrace::on(1)) BackendTrace::out("linkFlowAction", act + QStringLiteral(" ") + BackendTrace::brief(p)); });
#undef TRACE_SIG1
#undef TRACE_SIGMAP

    const QString path = defaultDbPath();
    if (!m_db->open(path)) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: cannot open db at %s\n",
                     path.toUtf8().constData());
        return;
    }
    if (!m_db->runInitialMigrationIfNeeded()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV2()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v2 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV3()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v3 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV4()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v4 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV5()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v5 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV6()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v6 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }
    if (!m_db->migrateToV7()) {
        std::fprintf(stderr, "[chiplinks-backend] FATAL: migration v7 failed (%s)\n",
                     m_db->lastError().toUtf8().constData());
        return;
    }

    m_docs      = std::make_unique<DocsRepo>(*m_db);
    m_toc       = std::make_unique<TocItemsRepo>(*m_db);
    m_links     = std::make_unique<LinksRepo>(*m_db);
    m_pins      = std::make_unique<PinsRepo>(*m_db);
    m_backlinks = std::make_unique<BacklinksRepo>(*m_db);
    m_recents   = std::make_unique<RecentsRepo>(*m_db);
    m_meta      = std::make_unique<MetaRepo>(*m_db);
    m_postits   = std::make_unique<PostitsRepo>(*m_db);
    m_settings  = std::make_unique<SettingsRepo>(*m_db);
    m_planner   = std::make_unique<PlannerRepo>(*m_db);

    m_notifications = std::make_unique<NotificationService>(m_planner.get(), this);
    connect(m_notifications.get(), &NotificationService::upcomingChanged,
            this, &ChiplinksBackend::plannerNotificationsChanged);
    connect(m_notifications.get(), &NotificationService::due,
            this, &ChiplinksBackend::plannerNotificationDue);

    connect(m_notifications.get(), &NotificationService::due,
            this, [this](const QVariantMap &) { emit plannerNotificationLogChanged(); });

    const QVariantMap counts = m_db->countRows();
    std::fprintf(stderr,
                 "[chiplinks-backend] ready schema=%d docs=%d toc_items=%d links=%d pins=%d recents=%d meta=%d\n",
                 m_db->userVersion(),
                 counts.value(QStringLiteral("docs"), -1).toInt(),
                 counts.value(QStringLiteral("toc_items"), -1).toInt(),
                 counts.value(QStringLiteral("links"), -1).toInt(),
                 counts.value(QStringLiteral("pins"), -1).toInt(),
                 counts.value(QStringLiteral("recents"), -1).toInt(),
                 counts.value(QStringLiteral("meta"), -1).toInt());
}

ChiplinksBackend::~ChiplinksBackend() = default;

QString ChiplinksBackend::hello() const {
    return QStringLiteral("ok");
}

QString ChiplinksBackend::dbInfo() {
    if (!m_db) return QStringLiteral("no db");
    const QVariantMap c = m_db->countRows();
    return QStringLiteral("schema=%1 docs=%2 toc_items=%3 links=%4 pins=%5 recents=%6 meta=%7")
        .arg(m_db->userVersion())
        .arg(c.value(QStringLiteral("docs"), -1).toInt())
        .arg(c.value(QStringLiteral("toc_items"), -1).toInt())
        .arg(c.value(QStringLiteral("links"), -1).toInt())
        .arg(c.value(QStringLiteral("pins"), -1).toInt())
        .arg(c.value(QStringLiteral("recents"), -1).toInt())
        .arg(c.value(QStringLiteral("meta"), -1).toInt());
}

QString ChiplinksBackend::memStats() {
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString err = QStringLiteral("[chiplinks-perf] memStats: cannot read /proc/self/status");
        std::fprintf(stderr, "%s\n", err.toUtf8().constData());
        return err;
    }
    QString vmRss = QStringLiteral("VmRSS:?");
    QString vmSize = QStringLiteral("VmSize:?");

    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("VmRSS:")) vmRss = QString::fromUtf8(line).simplified();
        else if (line.startsWith("VmSize:")) vmSize = QString::fromUtf8(line).simplified();
    }
    const QString out = QStringLiteral("[chiplinks-perf] mem %1 | %2").arg(vmSize, vmRss);
    std::fprintf(stderr, "%s\n", out.toUtf8().constData());
    return out;
}

void ChiplinksBackend::setTracing(int level) {
    BackendTrace::setLevel(level);
    std::fprintf(stderr, "[chiplinks-trace] level set to %d\n", BackendTrace::level());
}

bool ChiplinksBackend::upsertDoc(const QString &docId, const QString &name, const QString &parentId) {
    CL_CMD(QStringLiteral("%1 name=%2").arg(docId, name));
    if (!m_docs) return false;
    const bool ok = m_docs->upsert(docId, name, parentId);
    if (ok) emit docsChanged(docId);
    return ok;
}

bool ChiplinksBackend::markDocDeleted(const QString &docId) {
    CL_CMD(docId);
    if (!m_docs) return false;
    const bool ok = m_docs->markDeleted(docId);
    if (ok) emit docsChanged(docId);
    return ok;
}

bool ChiplinksBackend::restoreDoc(const QString &docId) {
    CL_CMD(docId);
    if (!m_docs) return false;
    const bool ok = m_docs->restore(docId);
    if (ok) emit docsChanged(docId);
    return ok;
}

QVariantList ChiplinksBackend::loadTocItems(const QString &docId) {
    CL_READ(docId);
    if (!m_toc) return {};
    return m_toc->loadForDoc(docId);
}

QVariantList ChiplinksBackend::loadTocCustom(const QString &docId) {
    CL_READ(docId);
    if (!m_toc) return {};
    return m_toc->loadTocCustom(docId);
}

bool ChiplinksBackend::upsertTocItem(const QVariantMap &item) {
    CL_CMD(BackendTrace::brief(item));
    if (!m_toc) return false;
    const bool ok = m_toc->upsert(item);
    if (ok) emit tocItemsChanged(item.value(QStringLiteral("doc_id")).toString());
    return ok;
}

bool ChiplinksBackend::deleteTocItem(const QString &id) {
    CL_CMD(id);
    if (!m_toc) return false;

    const QString docId = m_toc->docIdForItem(id);

    if (id.startsWith(QStringLiteral("xref:")) && m_db && m_links) {
        const QString srcDocId = m_links->sourceDocIdForLink(id);
        if (!m_db->beginTransaction()) {
            emit error(QStringLiteral("deleteTocItem"), QStringLiteral("BEGIN failed"));
            return false;
        }
        const bool tocOk  = m_toc->remove(id);
        const bool linkOk = m_links->remove(id);
        if (!tocOk && !linkOk) { m_db->rollback(); return false; }
        if (!m_db->commit()) { m_db->rollback(); return false; }
        if (tocOk)  emit tocItemsChanged(docId);
        if (linkOk) emit linksChanged(srcDocId);
        return true;
    }
    const bool ok = m_toc->remove(id);
    if (ok) emit tocItemsChanged(docId);
    return ok;
}

bool ChiplinksBackend::setTocItemLevel(const QString &id, int level) {
    CL_CMD(QStringLiteral("%1 level=%2").arg(id).arg(level));
    if (!m_toc) return false;
    const QString docId = m_toc->docIdForItem(id);
    const bool ok = m_toc->setLevel(id, level);
    if (ok) emit tocItemsChanged(docId);
    return ok;
}

bool ChiplinksBackend::restoreTocItem(const QString &id) {
    CL_CMD(id);
    if (!m_toc) return false;
    const QString docId = m_toc->docIdForItem(id);

    if (id.startsWith(QStringLiteral("xref:")) && m_db && m_links) {
        const QString srcDocId = m_links->sourceDocIdForLink(id);
        if (!m_db->beginTransaction()) {
            emit error(QStringLiteral("restoreTocItem"), QStringLiteral("BEGIN failed"));
            return false;
        }
        const bool tocOk  = m_toc->restore(id);
        const bool linkOk = m_links->restore(id);
        if (!tocOk && !linkOk) { m_db->rollback(); return false; }
        if (!m_db->commit()) { m_db->rollback(); return false; }
        if (tocOk)  emit tocItemsChanged(docId);
        if (linkOk) emit linksChanged(srcDocId);
        return true;
    }
    const bool ok = m_toc->restore(id);
    if (ok) emit tocItemsChanged(docId);
    return ok;
}

int ChiplinksBackend::deleteTocItems(const QStringList &ids) {
    CL_CMD(QStringLiteral("[%1]").arg(ids.size()));
    if (!m_toc || ids.isEmpty()) return 0;

    const QString docId = m_toc->docIdForItem(ids.first());
    const int n = m_toc->removeMany(ids);
    if (n > 0) emit tocItemsChanged(docId);
    return n;
}

bool ChiplinksBackend::reorderTocItem(const QString &id, const QString &newParentId, double newOrderIdx) {
    CL_CMD(QStringLiteral("%1 order=%2").arg(id).arg(newOrderIdx));
    if (!m_toc) return false;
    const QString docId = m_toc->docIdForItem(id);
    const bool ok = m_toc->reorder(id, newParentId, newOrderIdx);
    if (ok) emit tocItemsChanged(docId);
    return ok;
}

bool ChiplinksBackend::moveTocItemUp(const QString &id) {
    CL_CMD(id);
    if (!m_toc) return false;
    const QString docId = m_toc->docIdForItem(id);
    const bool changed = m_toc->moveTocItemUp(id);
    if (changed) emit tocItemsChanged(docId);
    return changed;
}

bool ChiplinksBackend::moveTocItemDown(const QString &id) {
    CL_CMD(id);
    if (!m_toc) return false;
    const QString docId = m_toc->docIdForItem(id);
    const bool changed = m_toc->moveTocItemDown(id);
    if (changed) emit tocItemsChanged(docId);
    return changed;
}

QStringList ChiplinksBackend::tocNumberPrefixes(const QVariantList &levels, const QString &style) {
    CL_READ(QStringLiteral("n=%1 style=%2").arg(levels.size()).arg(style));

    return TocNumbering::prefixes(levels, style);
}

QVariantList ChiplinksBackend::loadLinksForDoc(const QString &sourceDocId) {
    CL_READ(sourceDocId);
    if (!m_links) return {};
    return m_links->loadForDoc(sourceDocId);
}

bool ChiplinksBackend::upsertLink(const QVariantMap &link) {
    CL_CMD(BackendTrace::brief(link));
    if (!m_links) return false;
    const bool ok = m_links->upsert(link);
    if (ok) emit linksChanged(link.value(QStringLiteral("source_doc_id")).toString());
    return ok;
}

bool ChiplinksBackend::deleteLink(const QString &id) {
    CL_CMD(id);
    if (!m_links) return false;
    const QString srcDocId = m_links->sourceDocIdForLink(id);
    const bool ok = m_links->remove(id);
    if (ok) emit linksChanged(srcDocId);
    return ok;
}

QVariantList ChiplinksBackend::searchLinks(const QString &query, int limit) {
    CL_READ(QStringLiteral("q=%1 lim=%2").arg(query).arg(limit));
    if (!m_links) return {};
    return m_links->search(query, limit);
}

bool ChiplinksBackend::renameLink(const QString &id, const QString &name) {
    CL_CMD(QStringLiteral("%1 name=%2").arg(id, name));
    if (!m_db || !m_links || !m_toc) return false;

    const QString srcDocId = m_links->sourceDocIdForLink(id);
    const QString docId    = m_toc->docIdForItem(id);
    if (!m_db->beginTransaction()) {
        emit error(QStringLiteral("renameLink"), QStringLiteral("BEGIN failed"));
        return false;
    }
    const bool linkOk = m_links->rename(id, name);
    const bool tocOk  = m_toc->rename(id, name);
    if (!linkOk && !tocOk) {
        m_db->rollback();
        return false;
    }
    if (!m_db->commit()) { m_db->rollback(); return false; }
    if (tocOk)  emit tocItemsChanged(docId);
    if (linkOk) emit linksChanged(srcDocId);
    return true;
}

QVariantList ChiplinksBackend::loadPins(const QString &linkId) {
    CL_READ(linkId);
    if (!m_pins) return {};
    return m_pins->loadForLink(linkId);
}

QVariantList ChiplinksBackend::loadPinsForPage(const QString &docId, int page) {
    CL_READ(QStringLiteral("%1 p=%2").arg(docId).arg(page));
    if (!m_pins) return {};
    return m_pins->loadForPage(docId, page);
}

QVariantList ChiplinksBackend::loadPinsForDoc(const QString &docId) {
    CL_READ(docId);
    if (!m_pins) return {};
    return m_pins->loadForDoc(docId);
}

bool ChiplinksBackend::setPin(const QString &linkId, int page, qreal x, qreal y, qreal scale, int colorIdx) {
    CL_CMD(QStringLiteral("%1 p=%2 x=%3 y=%4").arg(linkId).arg(page).arg(x).arg(y));
    if (!m_pins) return false;
    const bool ok = m_pins->set(linkId, page, x, y, scale, colorIdx);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

bool ChiplinksBackend::pinSetColor(const QString &linkId, int page, int colorIdx) {
    CL_CMD(QStringLiteral("%1 p=%2 c=%3").arg(linkId).arg(page).arg(colorIdx));
    if (!m_pins) return false;
    const bool ok = m_pins->setColor(linkId, page, colorIdx);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

bool ChiplinksBackend::pinCycleColor(const QString &linkId, int page) {
    CL_CMD(QStringLiteral("%1 p=%2").arg(linkId).arg(page));
    if (!m_pins) return false;
    const bool ok = m_pins->cycleColor(linkId, page);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

bool ChiplinksBackend::pinSetPosition(const QString &linkId, int page, qreal x, qreal y,
                                      qreal minX, qreal maxX, qreal minY, qreal maxY) {
    CL_CMD(QStringLiteral("%1 p=%2 x=%3 y=%4").arg(linkId).arg(page).arg(x).arg(y));
    if (!m_pins) return false;
    const bool ok = m_pins->setPosition(linkId, page, x, y, minX, maxX, minY, maxY);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

bool ChiplinksBackend::pinNudge(const QString &linkId, int page, qreal dx, qreal dy,
                                qreal minX, qreal maxX, qreal minY, qreal maxY) {
    CL_CMD(QStringLiteral("%1 p=%2 d=%3,%4").arg(linkId).arg(page).arg(dx).arg(dy));
    if (!m_pins) return false;
    const bool ok = m_pins->nudge(linkId, page, dx, dy, minX, maxX, minY, maxY);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

QVariantMap ChiplinksBackend::setPinAutoPlace(const QString &docId, const QString &linkId, int page,
                                              qreal defaultX, qreal defaultY, qreal staggerY,
                                              qreal collisionRX, qreal collisionRY, qreal maxStaggerY) {
    CL_CMD(QStringLiteral("%1 p=%2").arg(linkId).arg(page));
    if (!m_pins) return {};
    QVariantMap r = m_pins->setAutoPlace(docId, linkId, page, defaultX, defaultY, staggerY,
                                         collisionRX, collisionRY, maxStaggerY);
    if (r.value(QStringLiteral("ok")).toBool()) emit pinsChanged(linkId);
    return r;
}

bool ChiplinksBackend::removePin(const QString &linkId, int page) {
    CL_CMD(QStringLiteral("%1 p=%2").arg(linkId).arg(page));
    if (!m_pins) return false;
    const bool ok = m_pins->remove(linkId, page);
    if (ok) emit pinsChanged(linkId);
    return ok;
}

void ChiplinksBackend::queryBacklinks(const QString &targetDocId, const QString &targetPageKey) {
    CL_CMD(QStringLiteral("%1:%2").arg(targetDocId, targetPageKey));
    if (!m_backlinks) return;

    const QString key = targetDocId + QLatin1Char(':') + targetPageKey;
    {
        QMutexLocker lock(&m_inflightMutex);
        if (m_inflightBacklinks.contains(key)) return;
        m_inflightBacklinks.insert(key);
    }

    QThread *worker = QThread::create([this, key, targetDocId, targetPageKey]() {
        const QVariantList list = m_backlinks->query(targetDocId, targetPageKey);
        {
            QMutexLocker lock(&m_inflightMutex);
            m_inflightBacklinks.remove(key);
        }
        QMetaObject::invokeMethod(this, "backlinksReady", Qt::QueuedConnection,
                                  Q_ARG(QString, targetDocId),
                                  Q_ARG(QString, targetPageKey),
                                  Q_ARG(QVariantList, list));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ChiplinksBackend::loadBacklinkGroups(const QString &targetDocId, const QString &targetPageKey) {
    CL_CMD(QStringLiteral("%1:%2").arg(targetDocId, targetPageKey));
    if (!m_backlinks) return;

    const QString key = targetDocId + QLatin1Char(':') + targetPageKey;
    {
        QMutexLocker lock(&m_inflightMutex);
        if (m_inflightBacklinkGroups.contains(key)) return;
        m_inflightBacklinkGroups.insert(key);
    }

    QThread *worker = QThread::create([this, key, targetDocId, targetPageKey]() {
        const QVariantList groups = m_backlinks->groupsForDoc(targetDocId, targetPageKey);
        {
            QMutexLocker lock(&m_inflightMutex);
            m_inflightBacklinkGroups.remove(key);
        }
        QMetaObject::invokeMethod(this, "backlinkGroupsReady", Qt::QueuedConnection,
                                  Q_ARG(QString, targetDocId),
                                  Q_ARG(QString, targetPageKey),
                                  Q_ARG(QVariantList, groups));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

int ChiplinksBackend::countBacklinks(const QString &targetDocId, const QString &targetPageKey) {
    CL_READ(QStringLiteral("%1:%2").arg(targetDocId, targetPageKey));
    if (!m_backlinks) return 0;
    return m_backlinks->count(targetDocId, targetPageKey);
}

QVariantList ChiplinksBackend::recentTargets(int limit) {
    CL_READ(QStringLiteral("lim=%1").arg(limit));
    if (!m_recents) return {};
    return m_recents->top(limit);
}

QVariantList ChiplinksBackend::recentsForDoc(const QString &srcDocId, int limit) {
    CL_READ(QStringLiteral("%1 lim=%2").arg(srcDocId).arg(limit));
    if (!m_recents) return {};
    return m_recents->topForDoc(srcDocId, limit);
}

QVariantList ChiplinksBackend::loadRecentGroups(const QString &srcDocId, int limit) {
    CL_READ(QStringLiteral("%1 lim=%2").arg(srcDocId).arg(limit));
    if (!m_recents) return {};
    return m_recents->groups(srcDocId, limit);
}

bool ChiplinksBackend::bumpRecent(const QString &targetDocId, const QString &targetPageKey,
                                  const QString &targetPageLabel) {
    CL_CMD(QStringLiteral("%1:%2").arg(targetDocId, targetPageKey));
    if (!m_recents) return false;
    const bool ok = m_recents->bump(targetDocId, targetPageKey, targetPageLabel);
    if (ok) emit recentsChanged();
    return ok;
}

bool ChiplinksBackend::forgetRecent(const QString &targetDocId, const QString &targetPageKey) {
    CL_CMD(QStringLiteral("%1:%2").arg(targetDocId, targetPageKey));
    if (!m_recents) return false;
    const bool ok = m_recents->forget(targetDocId, targetPageKey);
    if (ok) emit recentsChanged();
    return ok;
}

QString ChiplinksBackend::getMeta(const QString &key, const QString &defaultValue) {
    CL_READ(key);
    if (!m_meta) return defaultValue;
    return m_meta->get(key, defaultValue);
}

bool ChiplinksBackend::setMeta(const QString &key, const QString &value) {
    CL_CMD(QStringLiteral("%1=%2").arg(key, value));
    if (!m_meta) return false;
    const bool ok = m_meta->set(key, value);
    if (ok) emit metaChanged(key);
    return ok;
}

QVariantList ChiplinksBackend::settingsSchema(const QString &section) {
    CL_READ(section);
    if (!m_settings) return {};
    return m_settings->schemaFor(section);
}

QVariantMap ChiplinksBackend::loadSettings(const QString &section) {
    CL_READ(section);
    if (!m_settings) return {};
    return m_settings->loadSection(section);
}

bool ChiplinksBackend::setSetting(const QString &key, const QString &value) {
    CL_CMD(QStringLiteral("%1=%2").arg(key, value));
    if (!m_settings) return false;
    const QString section = m_settings->setClamped(key, value);
    if (section.isEmpty()) return false;
    emit settingsChanged(section);
    return true;
}

bool ChiplinksBackend::resetSettings(const QString &section) {
    CL_CMD(section);
    if (!m_settings) return false;
    const QString done = m_settings->resetSection(section);
    if (done.isEmpty()) return false;
    emit settingsChanged(done);
    return true;
}

QVariantList ChiplinksBackend::loadPostitsForPage(const QString &hostDocId, int page) {
    CL_READ(QStringLiteral("%1 p=%2").arg(hostDocId).arg(page));
    if (!m_postits) return {};
    return m_postits->loadForPage(hostDocId, page);
}

QVariantList ChiplinksBackend::loadAllPostits(bool includeDeleted, int limit) {
    CL_READ(QStringLiteral("incl=%1 lim=%2").arg(includeDeleted).arg(limit));
    if (!m_postits) return {};
    return m_postits->loadAll(includeDeleted, limit);
}

bool ChiplinksBackend::upsertPostit(const QVariantMap &postit) {
    CL_CMD(BackendTrace::brief(postit));
    if (!m_postits) return false;
    const bool ok = m_postits->upsert(postit);
    if (ok) emit postitsChanged(postit.value(QStringLiteral("host_doc_id")).toString());
    return ok;
}

bool ChiplinksBackend::setPostitGeometry(const QString &id, qreal x, qreal y, qreal scale, int colorIdx) {
    CL_CMD(QStringLiteral("%1 x=%2 y=%3").arg(id).arg(x).arg(y));
    if (!m_postits) return false;
    const bool ok = m_postits->setGeometry(id, x, y, scale, colorIdx);
    if (ok) emit postitsChanged(m_postits->hostDocIdForPostit(id));
    return ok;
}

bool ChiplinksBackend::setPostitTitle(const QString &id, const QString &title) {
    CL_CMD(QStringLiteral("%1 title=%2").arg(id, title));
    if (!m_postits) return false;
    const bool ok = m_postits->setTitle(id, title);
    if (ok) emit postitsChanged(m_postits->hostDocIdForPostit(id));
    return ok;
}

bool ChiplinksBackend::markPostitDeleted(const QString &id) {
    CL_CMD(id);
    if (!m_postits) return false;

    const QString hostDocId = m_postits->hostDocIdForPostit(id);
    const bool ok = m_postits->markDeleted(id);
    if (ok) emit postitsChanged(hostDocId);
    return ok;
}

bool ChiplinksBackend::restorePostit(const QString &id) {
    CL_CMD(id);
    if (!m_postits) return false;
    const bool ok = m_postits->restore(id);
    if (ok) emit postitsChanged(m_postits->hostDocIdForPostit(id));
    return ok;
}

int ChiplinksBackend::countPostitsForScratchPages(const QString &scratchDocId,
                                                  const QStringList &pageKeys) {
    CL_READ(QStringLiteral("%1 [%2]").arg(scratchDocId).arg(pageKeys.size()));
    if (!m_postits) return 0;
    return m_postits->countForScratchPages(scratchDocId, pageKeys);
}

QVariantList ChiplinksBackend::loadPlannerEvents(const QString &ymd) {
    CL_READ(ymd);
    if (!m_planner) return {};
    return m_planner->loadEventsForDay(ymd);
}

QVariantList ChiplinksBackend::loadPlannerMonth(int year, int month) {
    CL_READ(QStringLiteral("%1-%2").arg(year).arg(month));
    if (!m_planner) return {};
    return m_planner->loadMonthCells(year, month);
}

bool ChiplinksBackend::upsertPlannerEvent(const QVariantMap &e) {
    CL_CMD(BackendTrace::brief(e));
    if (!m_planner) return false;
    const bool ok = m_planner->upsertEvent(e);
    if (ok) emit plannerEventsChanged(e.value(QStringLiteral("ymd")).toString());
    return ok;
}

bool ChiplinksBackend::deletePlannerEvent(const QString &id) {
    CL_CMD(id);
    if (!m_planner) return false;
    const QString ymd = m_planner->ymdForEvent(id);
    const bool ok = m_planner->deleteEvent(id);
    if (ok) emit plannerEventsChanged(ymd);
    return ok;
}

QVariantList ChiplinksBackend::loadPlannerTasks(const QString &list, const QString &ymd, bool includeDone) {
    CL_READ(QStringLiteral("list=%1 ymd=%2").arg(list, ymd));
    if (!m_planner) return {};
    return m_planner->loadTasks(list, ymd, includeDone);
}

bool ChiplinksBackend::upsertPlannerTask(const QVariantMap &t) {
    CL_CMD(BackendTrace::brief(t));
    if (!m_planner) return false;
    const bool ok = m_planner->upsertTask(t);
    if (ok) emit plannerTasksChanged();
    return ok;
}

bool ChiplinksBackend::togglePlannerTask(const QString &id) {
    CL_CMD(id);
    if (!m_planner) return false;
    const bool ok = m_planner->toggleTask(id);
    if (ok) emit plannerTasksChanged();
    return ok;
}

bool ChiplinksBackend::deletePlannerTask(const QString &id) {
    CL_CMD(id);
    if (!m_planner) return false;
    const bool ok = m_planner->deleteTask(id);
    if (ok) emit plannerTasksChanged();
    return ok;
}

QVariantList ChiplinksBackend::loadPlannerHabits() {
    CL_READ(QString());
    if (!m_planner) return {};
    return m_planner->loadHabits();
}

QVariantList ChiplinksBackend::loadPlannerHabitMarks(const QString &monthPrefix) {
    CL_READ(monthPrefix);
    if (!m_planner) return {};
    return m_planner->loadHabitMarks(monthPrefix);
}

bool ChiplinksBackend::upsertPlannerHabit(const QVariantMap &h) {
    CL_CMD(BackendTrace::brief(h));
    if (!m_planner) return false;
    const bool ok = m_planner->upsertHabit(h);
    if (ok) emit plannerHabitsChanged();
    return ok;
}

bool ChiplinksBackend::setPlannerHabitMark(const QString &habitId, const QString &ymd, int value) {
    CL_CMD(QStringLiteral("%1 %2=%3").arg(habitId, ymd).arg(value));
    if (!m_planner) return false;
    const bool ok = m_planner->setHabitMark(habitId, ymd, value);
    if (ok) emit plannerHabitsChanged();
    return ok;
}

QVariantMap ChiplinksBackend::plannerInkBinding(const QString &pageKey) {
    CL_READ(pageKey);
    if (!m_planner) return {};
    return m_planner->inkBinding(pageKey);
}

QVariantList ChiplinksBackend::plannerInkBindingsLike(const QString &prefix) {
    CL_READ(prefix);
    if (!m_planner) return {};
    return m_planner->inkBindingsLike(prefix);
}

bool ChiplinksBackend::deletePlannerInkBinding(const QString &pageKey) {
    CL_CMD(pageKey);
    if (!m_planner) return false;
    const bool ok = m_planner->deleteInkBinding(pageKey);
    if (ok) emit plannerInkChanged(pageKey);
    return ok;
}

bool ChiplinksBackend::setPlannerInkBinding(const QString &pageKey, const QString &scratchDocId,
                                            const QString &scratchPage) {
    CL_CMD(QStringLiteral("%1 -> %2/%3").arg(pageKey, scratchDocId, scratchPage));
    if (!m_planner) return false;
    const bool ok = m_planner->setInkBinding(pageKey, scratchDocId, scratchPage);
    if (ok) emit plannerInkChanged(pageKey);
    return ok;
}

QVariantList ChiplinksBackend::loadPlannerEntries(const QString &kind, const QString &bucket) {
    CL_READ(QStringLiteral("%1/%2").arg(kind, bucket));
    if (!m_planner) return {};
    return m_planner->loadEntries(kind, bucket);
}

bool ChiplinksBackend::upsertPlannerEntry(const QVariantMap &e) {
    CL_CMD(BackendTrace::brief(e));
    if (!m_planner) return false;
    const bool ok = m_planner->upsertEntry(e);
    if (ok) emit plannerEntriesChanged(e.value(QStringLiteral("kind")).toString(),
                                       e.value(QStringLiteral("bucket")).toString());
    return ok;
}

bool ChiplinksBackend::deletePlannerEntry(const QString &id) {
    CL_CMD(id);
    if (!m_planner) return false;

    const QVariantMap meta = m_planner->entryById(id);
    const bool ok = m_planner->deleteEntry(id);
    if (ok) emit plannerEntriesChanged(meta.value(QStringLiteral("kind")).toString(),
                                       meta.value(QStringLiteral("bucket")).toString());
    return ok;
}

QVariantList ChiplinksBackend::plannerUpcoming(int withinSec) {
    CL_READ(QStringLiteral("within=%1").arg(withinSec));
    if (!m_planner) return {};
    return m_planner->loadUpcoming(QDateTime::currentSecsSinceEpoch(), withinSec);
}

void ChiplinksBackend::plannerNotificationsStart(int intervalSec, int windowSec) {
    CL_CMD(QStringLiteral("every=%1 window=%2").arg(intervalSec).arg(windowSec));
    if (m_notifications) m_notifications->start(intervalSec, windowSec);
}

void ChiplinksBackend::plannerNotificationsStop() {
    CL_CMD(QString());
    if (m_notifications) m_notifications->stop();
}

QVariantList ChiplinksBackend::plannerNotificationLog(int limit) {
    CL_READ(QStringLiteral("limit=%1").arg(limit));
    if (!m_planner) return {};
    return m_planner->loadNotifications(limit);
}

int ChiplinksBackend::plannerUnreadCount() {
    CL_READ(QString());
    if (!m_planner) return 0;
    return m_planner->unreadNotificationCount();
}

bool ChiplinksBackend::markPlannerNotificationRead(const QString &id) {
    CL_CMD(id);
    if (!m_planner) return false;
    const bool ok = m_planner->markNotificationRead(id);
    if (ok) emit plannerNotificationLogChanged();
    return ok;
}

bool ChiplinksBackend::markAllPlannerNotificationsRead() {
    CL_CMD(QString());
    if (!m_planner) return false;
    const bool ok = m_planner->markAllNotificationsRead();
    if (ok) emit plannerNotificationLogChanged();
    return ok;
}

bool ChiplinksBackend::clearPlannerNotifications() {
    CL_CMD(QString());
    if (!m_planner) return false;
    const bool ok = m_planner->clearNotifications();
    if (ok) emit plannerNotificationLogChanged();
    return ok;
}

bool ChiplinksBackend::snoozePlannerNotification(const QString &id, int mins) {
    CL_CMD(QStringLiteral("%1 +%2m").arg(id).arg(mins));
    if (!m_planner) return false;
    const qint64 until = QDateTime::currentSecsSinceEpoch() + static_cast<qint64>(mins) * 60;
    const bool ok = m_planner->snoozeNotification(id, until);
    if (ok) emit plannerNotificationLogChanged();
    return ok;
}

QVariantMap ChiplinksBackend::plannerStats() {
    CL_READ(QString());
    if (!m_planner) return {};
    return m_planner->stats();
}

void ChiplinksBackend::requestOpenPlanner() {
    CL_CMD(QString());
    emit openPlannerRequested();
}

bool ChiplinksBackend::plannerReset(const QString &scope) {
    CL_CMD(scope);
    if (!m_planner) return false;
    const bool ok = m_planner->reset(scope);
    if (ok) {
        emit plannerEventsChanged(QString());
        emit plannerTasksChanged();
        emit plannerHabitsChanged();
        emit plannerEntriesChanged(QString(), QString());
    }
    return ok;
}

int ChiplinksBackend::copyTocForDuplicate(const QString &sourceDocId, const QString &newDocId) {
    CL_CMD(QStringLiteral("%1 -> %2").arg(sourceDocId, newDocId));
    if (!m_toc) return 0;
    const int n = m_toc->copyForDuplicate(sourceDocId, newDocId);
    if (n > 0) emit tocItemsChanged(newDocId);
    return n;
}

bool ChiplinksBackend::addCrossDocTocItem(const QVariantMap &tocItem, const QVariantMap &link) {
    CL_CMD(BackendTrace::brief(tocItem));
    if (!m_db || !m_toc || !m_links) return false;
    if (!m_db->beginTransaction()) {
        emit error(QStringLiteral("addCrossDocTocItem"), QStringLiteral("BEGIN failed"));
        return false;
    }
    const bool tocOk  = m_toc->upsert(tocItem);
    const bool linkOk = m_links->upsert(link);
    if (!tocOk || !linkOk) {
        m_db->rollback();
        emit error(QStringLiteral("addCrossDocTocItem"),
                   QStringLiteral("toc=%1 link=%2").arg(tocOk).arg(linkOk));
        return false;
    }
    if (!m_db->commit()) {
        m_db->rollback();
        return false;
    }
    emit tocItemsChanged(tocItem.value(QStringLiteral("doc_id")).toString());
    emit linksChanged(link.value(QStringLiteral("source_doc_id")).toString());
    return true;
}

QString ChiplinksBackend::createLink(const QString &sourceDocId, int sourcePage,
                                     const QString &sourcePageKey, const QString &targetDocId,
                                     const QString &targetPageKey, const QString &targetPageLabel,
                                     const QString &targetDocName) {
    CL_CMD(QStringLiteral("%1 p=%2 -> %3:%4").arg(sourceDocId).arg(sourcePage).arg(targetDocId, targetPageKey));
    if (!m_db || !m_toc || !m_links || !m_docs || !m_recents) return QString();
    if (sourceDocId.isEmpty() || targetDocId.isEmpty()) return QString();

    const QString id  = LinksRepo::mintId();
    const QString key = targetPageKey;
    const QString name = targetDocName;

    QVariantMap toc;
    toc[QStringLiteral("id")]       = id;
    toc[QStringLiteral("doc_id")]   = sourceDocId;
    toc[QStringLiteral("name")]     = name;
    toc[QStringLiteral("level")]    = 0;
    toc[QStringLiteral("page_key")] = sourcePageKey;

    const QString metadata = QString::fromUtf8(QJsonDocument(
        QJsonObject{{QStringLiteral("source_page_key"), sourcePageKey}}).toJson(QJsonDocument::Compact));

    QVariantMap link;
    link[QStringLiteral("id")]                = id;
    link[QStringLiteral("source_doc_id")]     = sourceDocId;
    link[QStringLiteral("target_doc_id")]     = targetDocId;
    link[QStringLiteral("target_page_key")]   = key;
    link[QStringLiteral("name")]              = name;
    link[QStringLiteral("source_page")]       = (sourcePage >= 0) ? QVariant(sourcePage) : QVariant();
    link[QStringLiteral("target_page_label")] = targetPageLabel;
    link[QStringLiteral("metadata")]          = metadata;

    if (!m_db->beginTransaction()) {
        emit error(QStringLiteral("createLink"), QStringLiteral("BEGIN failed"));
        return QString();
    }
    m_docs->upsert(targetDocId, name, QString());
    const bool tocOk  = m_toc->upsert(toc);
    const bool linkOk = m_links->upsert(link);
    if (!tocOk || !linkOk) {
        m_db->rollback();
        emit error(QStringLiteral("createLink"), QStringLiteral("toc=%1 link=%2").arg(tocOk).arg(linkOk));
        return QString();
    }
    if (!m_db->commit()) { m_db->rollback(); return QString(); }

    emit tocItemsChanged(sourceDocId);
    emit linksChanged(sourceDocId);
    if (m_recents->bump(targetDocId, key, targetPageLabel)) emit recentsChanged();
    return id;
}

QString ChiplinksBackend::linkFlowPhase() const {
    return m_linkFlow ? m_linkFlow->phaseString() : QStringLiteral("idle");
}

bool ChiplinksBackend::linkFlowCanReturn() const {
    return m_linkFlow && m_linkFlow->canReturn();
}

#define LF_DISPATCH(call) \
    if (!m_linkFlow) return; \
    const QString _before = m_linkFlow->phaseString(); \
    const bool _cbefore = m_linkFlow->canReturn(); \
    const QVariantMap _a = (call); \
    const QString _act = _a.value(QStringLiteral("action")).toString(); \
    if (_act != QStringLiteral("none")) emit linkFlowAction(_act, _a); \
    if (m_linkFlow->phaseString() != _before) emit linkFlowPhaseChanged(); \
    if (m_linkFlow->canReturn() != _cbefore) emit linkFlowCanReturnChanged()

void ChiplinksBackend::linkFlowStartSelection(const QString &sourceDocId, const QString &sourceDocName, int sourcePage) {
    CL_CMD(QStringLiteral("%1 p=%2").arg(sourceDocId).arg(sourcePage));
    LF_DISPATCH(m_linkFlow->startSelection(sourceDocId, sourceDocName, sourcePage));
}

void ChiplinksBackend::linkFlowSelectTarget(const QString &targetDocId, const QString &targetPageKey,
                                            const QString &targetDocName, const QString &targetPageLabel,
                                            const QString &curDocId) {
    CL_CMD(QStringLiteral("tgt=%1 cur=%2").arg(targetDocId, curDocId));
    LF_DISPATCH(m_linkFlow->selectTarget(targetDocId, targetPageKey, targetDocName, targetPageLabel, curDocId));
}

void ChiplinksBackend::linkFlowNavigate(const QString &xrefKey, const QString &targetDocId,
                                        const QString &targetPageKey, const QString &curDocId,
                                        int curPage, bool exists, bool archived) {
    CL_CMD(QStringLiteral("%1 tgt=%2 exists=%3 arch=%4").arg(xrefKey, targetDocId).arg(exists).arg(archived));
    LF_DISPATCH(m_linkFlow->navigate(xrefKey, targetDocId, targetPageKey, curDocId, curPage, exists, archived));
}

void ChiplinksBackend::linkFlowNavigateToPage(const QString &docId, int page,
                                              const QString &curDocId, int curPage) {
    CL_CMD(QStringLiteral("%1 p=%2 cur=%3").arg(docId).arg(page).arg(curDocId));
    LF_DISPATCH(m_linkFlow->navigateToPage(docId, page, curDocId, curPage));
}

void ChiplinksBackend::linkFlowDocBecameCurrent(const QString &docId, quint64 flowId) {
    CL_CMD(QStringLiteral("%1 f=%2").arg(docId).arg(flowId));
    LF_DISPATCH(m_linkFlow->docBecameCurrent(docId, flowId));
}

void ChiplinksBackend::linkFlowTocReady(const QString &docId, quint64 flowId) {
    CL_CMD(QStringLiteral("%1 f=%2").arg(docId).arg(flowId));
    LF_DISPATCH(m_linkFlow->tocReady(docId, flowId));
}

void ChiplinksBackend::linkFlowReturn(const QString &curDocId) {
    CL_CMD(curDocId);
    LF_DISPATCH(m_linkFlow->returnToSource(curDocId));
}

void ChiplinksBackend::linkFlowCancel() {
    CL_CMD(QString());
    LF_DISPATCH(m_linkFlow->cancel());
}

#undef LF_DISPATCH

void ChiplinksBackend::purgeSoftDeleted(int olderThanDays) {
    CL_CMD(QStringLiteral("days=%1").arg(olderThanDays));
    if (!m_db) return;
    const int n = m_db->purgeSoftDeleted(olderThanDays);
    emit maintenanceDone(QStringLiteral("purgeSoftDeleted"), n);
}

void ChiplinksBackend::vacuum() {
    CL_CMD(QString());
    if (!m_db) return;
    const bool ok = m_db->incrementalVacuum();
    emit maintenanceDone(QStringLiteral("vacuum"), ok ? 1 : 0);
}

void ChiplinksBackend::stats(int topN) {
    CL_CMD(QStringLiteral("topN=%1").arg(topN));
    if (!m_db) { emit error(QStringLiteral("stats"), QStringLiteral("no database")); return; }
    QThread *worker = QThread::create([this, topN]() {
        const QVariantMap s = m_db->stats(topN);
        QMetaObject::invokeMethod(this, "statsReady", Qt::QueuedConnection,
                                  Q_ARG(QVariantMap, s));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ChiplinksBackend::graphData(int maxNodes) {
    CL_CMD(QStringLiteral("max=%1").arg(maxNodes));
    if (!m_db || !m_links) { emit error(QStringLiteral("graphData"), QStringLiteral("no database")); return; }
    QThread *worker = QThread::create([this, maxNodes]() {
        const QVariantMap g = m_links->graphData(maxNodes);
        QMetaObject::invokeMethod(this, "graphDataReady", Qt::QueuedConnection,
                                  Q_ARG(QVariantMap, g));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ChiplinksBackend::graphLayout(int maxNodes, const QVariantMap &params) {
    CL_CMD(QStringLiteral("max=%1 %2").arg(maxNodes).arg(BackendTrace::brief(params)));
    if (!m_db || !m_links) { emit error(QStringLiteral("graphLayout"), QStringLiteral("no database")); return; }
    QThread *worker = QThread::create([this, maxNodes, params]() {
        QVariantMap g = m_links->graphData(maxNodes);

        g[QStringLiteral("nodes")] = GraphLayout::layout(g.value(QStringLiteral("nodes")).toList(),
                                                         g.value(QStringLiteral("edges")).toList(),
                                                         params);
        QMetaObject::invokeMethod(this, "graphLayoutReady", Qt::QueuedConnection,
                                  Q_ARG(QVariantMap, g));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ChiplinksBackend::docGraph(const QString &docId, const QVariantMap &params) {
    CL_CMD(docId);
    if (!m_db || !m_toc) { emit error(QStringLiteral("docGraph"), QStringLiteral("no database")); return; }
    QThread *worker = QThread::create([this, docId, params]() {
        const QVariantMap lg = m_toc->docXrefGraph(docId);
        QVariantMap state;
        state[QStringLiteral("nodes")] = GraphLayout::layout(lg.value(QStringLiteral("nodes")).toList(),
                                                            lg.value(QStringLiteral("edges")).toList(),
                                                            params);
        state[QStringLiteral("edges")] = lg.value(QStringLiteral("edges"));
        QMetaObject::invokeMethod(this, "docGraphReady", Qt::QueuedConnection,
                                  Q_ARG(QString, docId), Q_ARG(QVariantMap, state));
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ChiplinksBackend::resetDatabase() {
    CL_CMD(QString());
    if (!m_db) return;
    if (m_resetInFlight) {
        std::fprintf(stderr, "[chiplinks-backend] resetDatabase ignored: already in flight\n");
        return;
    }
    m_resetInFlight = true;

    const int n = m_db->resetAll();
    m_resetInFlight = false;
    if (n < 0) {
        emit error(QStringLiteral("resetDatabase"), m_db->lastError());
        return;
    }

    emit docsChanged(QString());
    emit tocItemsChanged(QString());
    emit linksChanged(QString());
    emit pinsChanged(QString());
    emit postitsChanged(QString());
    emit recentsChanged();
    emit metaChanged(QString());
    emit maintenanceDone(QStringLiteral("resetDatabase"), n);
}

QVariantList ChiplinksBackend::loadDocs(bool includeDeleted, int limit) {
    CL_READ(QStringLiteral("incl=%1 lim=%2").arg(includeDeleted).arg(limit));
    if (!m_docs) return {};
    return m_docs->loadAll(includeDeleted, limit);
}

QVariantList ChiplinksBackend::loadAllLinks(bool includeDeleted, int limit) {
    CL_READ(QStringLiteral("incl=%1 lim=%2").arg(includeDeleted).arg(limit));
    if (!m_links) return {};
    return m_links->loadAll(includeDeleted, limit);
}
