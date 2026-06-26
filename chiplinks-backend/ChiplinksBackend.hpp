#pragma once

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

class Database;
class DocsRepo;
class TocItemsRepo;
class LinksRepo;
class PinsRepo;
class BacklinksRepo;
class RecentsRepo;
class MetaRepo;
class PostitsRepo;
class SettingsRepo;
class PlannerRepo;
class NotificationService;
class LinkFlow;

class ChiplinksBackend : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString linkFlowPhase READ linkFlowPhase NOTIFY linkFlowPhaseChanged)

    Q_PROPERTY(bool linkFlowCanReturn READ linkFlowCanReturn NOTIFY linkFlowCanReturnChanged)
public:
    explicit ChiplinksBackend(QObject *parent = nullptr);
    ~ChiplinksBackend() override;

    Q_INVOKABLE QString hello() const;
    Q_INVOKABLE QString dbInfo();

    Q_INVOKABLE QString memStats();

    Q_INVOKABLE void setTracing(int level);

    Q_INVOKABLE bool upsertDoc(const QString &docId, const QString &name, const QString &parentId);
    Q_INVOKABLE bool markDocDeleted(const QString &docId);
    Q_INVOKABLE bool restoreDoc(const QString &docId);

    Q_INVOKABLE QVariantList loadTocItems(const QString &docId);

    Q_INVOKABLE QVariantList loadTocCustom(const QString &docId);
    Q_INVOKABLE bool upsertTocItem(const QVariantMap &item);

    Q_INVOKABLE bool deleteTocItem(const QString &id);

    Q_INVOKABLE bool setTocItemLevel(const QString &id, int level);

    Q_INVOKABLE bool restoreTocItem(const QString &id);

    Q_INVOKABLE int  deleteTocItems(const QStringList &ids);
    Q_INVOKABLE bool reorderTocItem(const QString &id, const QString &newParentId, double newOrderIdx);

    Q_INVOKABLE bool moveTocItemUp(const QString &id);
    Q_INVOKABLE bool moveTocItemDown(const QString &id);

    Q_INVOKABLE QStringList tocNumberPrefixes(const QVariantList &levels, const QString &style);

    Q_INVOKABLE QVariantList loadLinksForDoc(const QString &sourceDocId);
    Q_INVOKABLE bool upsertLink(const QVariantMap &link);
    Q_INVOKABLE bool deleteLink(const QString &id);

    Q_INVOKABLE bool renameLink(const QString &id, const QString &name);
    Q_INVOKABLE QVariantList searchLinks(const QString &query, int limit);

    Q_INVOKABLE QVariantList loadPins(const QString &linkId);
    Q_INVOKABLE QVariantList loadPinsForPage(const QString &docId, int page);

    Q_INVOKABLE QVariantList loadPinsForDoc(const QString &docId);
    Q_INVOKABLE bool setPin(const QString &linkId, int page, qreal x, qreal y, qreal scale, int colorIdx);

    Q_INVOKABLE bool pinSetColor(const QString &linkId, int page, int colorIdx);
    Q_INVOKABLE bool pinCycleColor(const QString &linkId, int page);
    Q_INVOKABLE bool pinSetPosition(const QString &linkId, int page, qreal x, qreal y,
                                    qreal minX, qreal maxX, qreal minY, qreal maxY);
    Q_INVOKABLE bool pinNudge(const QString &linkId, int page, qreal dx, qreal dy,
                              qreal minX, qreal maxX, qreal minY, qreal maxY);

    Q_INVOKABLE QVariantMap setPinAutoPlace(const QString &docId, const QString &linkId, int page,
                                            qreal defaultX, qreal defaultY, qreal staggerY,
                                            qreal collisionRX, qreal collisionRY, qreal maxStaggerY);
    Q_INVOKABLE bool removePin(const QString &linkId, int page);

    Q_INVOKABLE void queryBacklinks(const QString &targetDocId, const QString &targetPageKey);

    Q_INVOKABLE void loadBacklinkGroups(const QString &targetDocId, const QString &targetPageKey);
    Q_INVOKABLE int  countBacklinks(const QString &targetDocId, const QString &targetPageKey);

    Q_INVOKABLE QVariantList recentTargets(int limit);

    Q_INVOKABLE QVariantList recentsForDoc(const QString &srcDocId, int limit);

    Q_INVOKABLE QVariantList loadRecentGroups(const QString &srcDocId, int limit);
    Q_INVOKABLE bool bumpRecent(const QString &targetDocId, const QString &targetPageKey,
                                const QString &targetPageLabel = QString());
    Q_INVOKABLE bool forgetRecent(const QString &targetDocId, const QString &targetPageKey);

    Q_INVOKABLE QString getMeta(const QString &key, const QString &defaultValue);
    Q_INVOKABLE bool    setMeta(const QString &key, const QString &value);

    Q_INVOKABLE QVariantList settingsSchema(const QString &section);
    Q_INVOKABLE QVariantMap  loadSettings(const QString &section);
    Q_INVOKABLE bool         setSetting(const QString &key, const QString &value);
    Q_INVOKABLE bool         resetSettings(const QString &section);

    Q_INVOKABLE QVariantList loadPostitsForPage(const QString &hostDocId, int page);
    Q_INVOKABLE QVariantList loadAllPostits(bool includeDeleted, int limit);
    Q_INVOKABLE bool upsertPostit(const QVariantMap &postit);
    Q_INVOKABLE bool setPostitGeometry(const QString &id, qreal x, qreal y, qreal scale, int colorIdx);
    Q_INVOKABLE bool setPostitTitle(const QString &id, const QString &title);
    Q_INVOKABLE bool markPostitDeleted(const QString &id);
    Q_INVOKABLE bool restorePostit(const QString &id);

    Q_INVOKABLE int  countPostitsForScratchPages(const QString &scratchDocId,
                                                 const QStringList &pageKeys);

    Q_INVOKABLE QVariantList loadPlannerEvents(const QString &ymd);

    Q_INVOKABLE QVariantList loadPlannerMonth(int year, int month);
    Q_INVOKABLE bool         upsertPlannerEvent(const QVariantMap &e);
    Q_INVOKABLE bool         deletePlannerEvent(const QString &id);
    Q_INVOKABLE QVariantList loadPlannerTasks(const QString &list, const QString &ymd, bool includeDone);
    Q_INVOKABLE bool         upsertPlannerTask(const QVariantMap &t);
    Q_INVOKABLE bool         togglePlannerTask(const QString &id);
    Q_INVOKABLE bool         deletePlannerTask(const QString &id);
    Q_INVOKABLE QVariantList loadPlannerHabits();
    Q_INVOKABLE QVariantList loadPlannerHabitMarks(const QString &monthPrefix);
    Q_INVOKABLE bool         upsertPlannerHabit(const QVariantMap &h);
    Q_INVOKABLE bool         setPlannerHabitMark(const QString &habitId, const QString &ymd, int value);

    Q_INVOKABLE QVariantMap  plannerInkBinding(const QString &pageKey);
    Q_INVOKABLE QVariantList plannerInkBindingsLike(const QString &prefix);
    Q_INVOKABLE bool         deletePlannerInkBinding(const QString &pageKey);
    Q_INVOKABLE bool         setPlannerInkBinding(const QString &pageKey,
                                                  const QString &scratchDocId,
                                                  const QString &scratchPage);

    Q_INVOKABLE QVariantList loadPlannerEntries(const QString &kind, const QString &bucket);
    Q_INVOKABLE bool         upsertPlannerEntry(const QVariantMap &e);
    Q_INVOKABLE bool         deletePlannerEntry(const QString &id);

    Q_INVOKABLE QVariantList plannerUpcoming(int withinSec);
    Q_INVOKABLE void         plannerNotificationsStart(int intervalSec, int windowSec);
    Q_INVOKABLE void         plannerNotificationsStop();

    Q_INVOKABLE QVariantList plannerNotificationLog(int limit);
    Q_INVOKABLE int          plannerUnreadCount();
    Q_INVOKABLE bool         markPlannerNotificationRead(const QString &id);
    Q_INVOKABLE bool         markAllPlannerNotificationsRead();
    Q_INVOKABLE bool         clearPlannerNotifications();
    Q_INVOKABLE bool         snoozePlannerNotification(const QString &id, int mins);

    Q_INVOKABLE QVariantMap  plannerStats();
    Q_INVOKABLE bool         plannerReset(const QString &scope);

    Q_INVOKABLE void         requestOpenPlanner();

    Q_INVOKABLE int copyTocForDuplicate(const QString &sourceDocId, const QString &newDocId);

    Q_INVOKABLE bool addCrossDocTocItem(const QVariantMap &tocItem, const QVariantMap &link);

    Q_INVOKABLE QString createLink(const QString &sourceDocId, int sourcePage,
                                   const QString &sourcePageKey, const QString &targetDocId,
                                   const QString &targetPageKey, const QString &targetPageLabel,
                                   const QString &targetDocName);

    QString linkFlowPhase() const;
    bool    linkFlowCanReturn() const;
    Q_INVOKABLE void linkFlowStartSelection(const QString &sourceDocId, const QString &sourceDocName, int sourcePage);
    Q_INVOKABLE void linkFlowSelectTarget(const QString &targetDocId, const QString &targetPageKey,
                                          const QString &targetDocName, const QString &targetPageLabel,
                                          const QString &curDocId);
    Q_INVOKABLE void linkFlowNavigate(const QString &xrefKey, const QString &targetDocId,
                                      const QString &targetPageKey, const QString &curDocId,
                                      int curPage, bool exists, bool archived);
    Q_INVOKABLE void linkFlowNavigateToPage(const QString &docId, int page,
                                            const QString &curDocId, int curPage);

    Q_INVOKABLE void linkFlowDocBecameCurrent(const QString &docId, quint64 flowId);
    Q_INVOKABLE void linkFlowTocReady(const QString &docId, quint64 flowId);
    Q_INVOKABLE void linkFlowReturn(const QString &curDocId);
    Q_INVOKABLE void linkFlowCancel();

    Q_INVOKABLE void purgeSoftDeleted(int olderThanDays);
    Q_INVOKABLE void vacuum();

    Q_INVOKABLE void stats(int topN);
    Q_INVOKABLE void graphData(int maxNodes);

    Q_INVOKABLE void graphLayout(int maxNodes, const QVariantMap &params);

    Q_INVOKABLE void docGraph(const QString &docId, const QVariantMap &params);
    Q_INVOKABLE void resetDatabase();
    Q_INVOKABLE QVariantList loadDocs(bool includeDeleted, int limit);
    Q_INVOKABLE QVariantList loadAllLinks(bool includeDeleted, int limit);

    Q_INVOKABLE bool captureAreaAsPng(int x, int y, int w, int h,
                                      int centerX, int centerY);

    Q_INVOKABLE void discardCaptureFile(const QString &fileUrl);

signals:
    void captureReady(const QString &fileUrl, int centerX, int centerY);
    void docsChanged(const QString &docId);
    void tocItemsChanged(const QString &docId);
    void linksChanged(const QString &sourceDocId);
    void pinsChanged(const QString &linkId);
    void postitsChanged(const QString &hostDocId);
    void recentsChanged();
    void metaChanged(const QString &key);
    void settingsChanged(const QString &section);

    void plannerEventsChanged(const QString &ymd);
    void plannerTasksChanged();
    void plannerHabitsChanged();
    void plannerEntriesChanged(const QString &kind, const QString &bucket);
    void plannerInkChanged(const QString &pageKey);

    void plannerNotificationsChanged(const QVariantList &items);
    void plannerNotificationDue(const QVariantMap &item);
    void plannerNotificationLogChanged();
    void openPlannerRequested();
    void backlinksReady(const QString &targetDocId, const QString &targetPageKey, const QVariantList &list);
    void backlinkGroupsReady(const QString &targetDocId, const QString &targetPageKey, const QVariantList &groups);
    void maintenanceDone(const QString &operation, int affected);
    void error(const QString &operation, const QString &message);
    void statsReady(const QVariantMap &stats);
    void graphDataReady(const QVariantMap &graph);
    void graphLayoutReady(const QVariantMap &state);
    void docGraphReady(const QString &docId, const QVariantMap &state);

    void linkFlowAction(const QString &action, const QVariantMap &payload);
    void linkFlowPhaseChanged();
    void linkFlowCanReturnChanged();

private:
    std::unique_ptr<Database>      m_db;
    std::unique_ptr<DocsRepo>      m_docs;
    std::unique_ptr<TocItemsRepo>  m_toc;
    std::unique_ptr<LinksRepo>     m_links;
    std::unique_ptr<PinsRepo>      m_pins;
    std::unique_ptr<BacklinksRepo> m_backlinks;
    std::unique_ptr<RecentsRepo>   m_recents;
    std::unique_ptr<MetaRepo>      m_meta;
    std::unique_ptr<PostitsRepo>   m_postits;
    std::unique_ptr<SettingsRepo>  m_settings;
    std::unique_ptr<PlannerRepo>   m_planner;
    std::unique_ptr<NotificationService> m_notifications;
    std::unique_ptr<LinkFlow>      m_linkFlow;

    QMutex          m_inflightMutex;
    QSet<QString>   m_inflightBacklinks;
    QSet<QString>   m_inflightBacklinkGroups;
    bool            m_resetInFlight = false;

    std::atomic<bool> m_captureInFlight{false};
};
