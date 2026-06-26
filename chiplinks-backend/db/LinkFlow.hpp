#pragma once

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

class LinkFlow {
public:

    enum class Phase { Idle, Selecting, AwaitingReturn, Opening, Resolving };

    QVariantMap startSelection(const QString &sourceDocId, const QString &sourceDocName, int sourcePage);

    QVariantMap selectTarget(const QString &targetDocId, const QString &targetPageKey,
                             const QString &targetDocName, const QString &targetPageLabel,
                             const QString &curDocId);

    QVariantMap navigate(const QString &xrefKey, const QString &targetDocId,
                         const QString &targetPageKey, const QString &curDocId,
                         int curPage, bool exists, bool archived);

    QVariantMap navigateToPage(const QString &docId, int page,
                               const QString &curDocId, int curPage);

    QVariantMap docBecameCurrent(const QString &docId, quint64 flowId);

    QVariantMap tocReady(const QString &docId, quint64 flowId);

    QVariantMap returnToSource(const QString &curDocId);

    QVariantMap cancel();

    Phase   phase() const { return m_phase; }
    QString phaseString() const;

    bool    canReturn() const { return !m_returnDocId.isEmpty(); }

    quint64 flowId() const { return m_flowId; }

private:
    Phase   m_phase = Phase::Idle;

    QString m_sourceDocId, m_sourceDocName;
    int     m_sourcePage = -1;
    QString m_pendingTargetDocId, m_pendingTargetPageKey, m_pendingTargetDocName, m_pendingTargetPageLabel;

    QString m_navTargetDocId, m_navTargetPageKey, m_navXrefKey;

    QString m_returnDocId;
    int     m_returnPage = -1;

    quint64 m_flowId = 0;

    void reset();
};
