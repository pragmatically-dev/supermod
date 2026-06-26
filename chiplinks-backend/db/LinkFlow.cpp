#include "LinkFlow.hpp"

namespace {
QVariantMap act(const QString &name) {
    QVariantMap m;
    m[QStringLiteral("action")] = name;
    return m;
}
}

QString LinkFlow::phaseString() const {
    switch (m_phase) {
    case Phase::Idle:           return QStringLiteral("idle");
    case Phase::Selecting:      return QStringLiteral("selecting");
    case Phase::AwaitingReturn: return QStringLiteral("awaitingReturn");
    case Phase::Opening:        return QStringLiteral("opening");
    case Phase::Resolving:      return QStringLiteral("resolving");
    }
    return QStringLiteral("idle");
}

void LinkFlow::reset() {

    m_phase = Phase::Idle;
    m_sourceDocId.clear();  m_sourceDocName.clear();  m_sourcePage = -1;
    m_pendingTargetDocId.clear(); m_pendingTargetPageKey.clear(); m_pendingTargetDocName.clear();
    m_pendingTargetPageLabel.clear();
    m_navTargetDocId.clear(); m_navTargetPageKey.clear(); m_navXrefKey.clear();
}

QVariantMap LinkFlow::startSelection(const QString &sourceDocId, const QString &sourceDocName, int sourcePage) {
    ++m_flowId;
    m_phase         = Phase::Selecting;
    m_sourceDocId   = sourceDocId;
    m_sourceDocName = sourceDocName;
    m_sourcePage    = sourcePage;

    return act(QStringLiteral("closeSidebar"));
}

QVariantMap LinkFlow::selectTarget(const QString &targetDocId, const QString &targetPageKey,
                                   const QString &targetDocName, const QString &targetPageLabel,
                                   const QString &curDocId) {
    if (m_phase != Phase::Selecting) return act(QStringLiteral("none"));

    if (curDocId == m_sourceDocId) {

        QVariantMap a = act(QStringLiteral("requestCreateLink"));
        a[QStringLiteral("sourceDocId")]     = m_sourceDocId;
        a[QStringLiteral("targetDocId")]     = targetDocId;
        a[QStringLiteral("targetPageKey")]   = targetPageKey;
        a[QStringLiteral("targetDocName")]   = targetDocName;
        a[QStringLiteral("targetPageLabel")] = targetPageLabel;
        a[QStringLiteral("sourcePage")]      = m_sourcePage;
        reset();
        return a;
    }

    ++m_flowId;
    m_pendingTargetDocId     = targetDocId;
    m_pendingTargetPageKey   = targetPageKey;
    m_pendingTargetDocName   = targetDocName;
    m_pendingTargetPageLabel = targetPageLabel;
    m_phase                  = Phase::AwaitingReturn;

    QVariantMap a = act(QStringLiteral("openDocAtPage"));
    a[QStringLiteral("docId")]  = m_sourceDocId;
    a[QStringLiteral("page")]   = m_sourcePage;
    a[QStringLiteral("flowId")] = m_flowId;
    return a;
}

QVariantMap LinkFlow::navigate(const QString &xrefKey, const QString &targetDocId,
                               const QString &targetPageKey, const QString &curDocId,
                               int curPage, bool exists, bool archived) {
    if (!exists) {
        QVariantMap a = act(QStringLiteral("showError"));
        a[QStringLiteral("xrefKey")] = xrefKey;
        return a;
    }
    if (archived) {
        QVariantMap a = act(QStringLiteral("showDownload"));
        a[QStringLiteral("targetDocId")]   = targetDocId;
        a[QStringLiteral("targetPageKey")] = targetPageKey;
        return a;
    }
    if (targetDocId == curDocId) {

        m_returnDocId = curDocId;
        m_returnPage  = curPage;
        QVariantMap a = act(QStringLiteral("resolveAndOpenPage"));
        a[QStringLiteral("pageKey")] = targetPageKey;
        a[QStringLiteral("xrefKey")] = xrefKey;
        return a;
    }

    ++m_flowId;
    m_returnDocId      = curDocId;
    m_returnPage       = curPage;
    m_navTargetDocId   = targetDocId;
    m_navTargetPageKey = targetPageKey;
    m_navXrefKey       = xrefKey;
    m_phase            = Phase::Opening;

    QVariantMap a = act(QStringLiteral("openDocAtPage"));
    a[QStringLiteral("docId")]  = targetDocId;
    a[QStringLiteral("page")]   = 0;
    a[QStringLiteral("flowId")] = m_flowId;
    return a;
}

QVariantMap LinkFlow::navigateToPage(const QString &docId, int page,
                                     const QString &curDocId, int curPage) {

    m_returnDocId = curDocId;
    m_returnPage  = curPage;

    if (docId == curDocId) {
        QVariantMap a = act(QStringLiteral("openPageHere"));
        a[QStringLiteral("page")] = page;
        return a;
    }

    ++m_flowId;
    QVariantMap a = act(QStringLiteral("openDocAtPage"));
    a[QStringLiteral("docId")]  = docId;
    a[QStringLiteral("page")]   = page;
    a[QStringLiteral("flowId")] = m_flowId;
    return a;
}

QVariantMap LinkFlow::docBecameCurrent(const QString &docId, quint64 flowId) {

    if (m_phase == Phase::Opening && flowId == m_flowId && docId == m_navTargetDocId) {
        m_phase = Phase::Resolving;
        return act(QStringLiteral("none"));
    }
    return act(QStringLiteral("none"));
}

QVariantMap LinkFlow::tocReady(const QString &docId, quint64 flowId) {

    if (m_phase == Phase::AwaitingReturn && flowId == m_flowId && docId == m_sourceDocId) {
        QVariantMap a = act(QStringLiteral("requestCreateLink"));
        a[QStringLiteral("sourceDocId")]     = m_sourceDocId;
        a[QStringLiteral("targetDocId")]     = m_pendingTargetDocId;
        a[QStringLiteral("targetPageKey")]   = m_pendingTargetPageKey;
        a[QStringLiteral("targetDocName")]   = m_pendingTargetDocName;
        a[QStringLiteral("targetPageLabel")] = m_pendingTargetPageLabel;
        a[QStringLiteral("sourcePage")]      = m_sourcePage;
        reset();
        return a;
    }

    if ((m_phase == Phase::Resolving || m_phase == Phase::Opening)
        && flowId == m_flowId && docId == m_navTargetDocId) {
        QVariantMap a = act(QStringLiteral("resolveAndOpenPage"));
        a[QStringLiteral("pageKey")] = m_navTargetPageKey;
        a[QStringLiteral("xrefKey")] = m_navXrefKey;
        reset();
        return a;
    }
    return act(QStringLiteral("none"));
}

QVariantMap LinkFlow::returnToSource(const QString &curDocId) {
    if (m_returnDocId.isEmpty()) return act(QStringLiteral("none"));
    const QString doc  = m_returnDocId;
    const int     page = m_returnPage;
    m_returnDocId.clear();
    m_returnPage = -1;

    if (doc == curDocId) {
        QVariantMap a = act(QStringLiteral("openPageHere"));
        a[QStringLiteral("page")] = page;
        return a;
    }
    ++m_flowId;
    QVariantMap a = act(QStringLiteral("openDocAtPage"));
    a[QStringLiteral("docId")]  = doc;
    a[QStringLiteral("page")]   = page;
    a[QStringLiteral("flowId")] = m_flowId;
    return a;
}

QVariantMap LinkFlow::cancel() {
    ++m_flowId;
    reset();
    return act(QStringLiteral("none"));
}
