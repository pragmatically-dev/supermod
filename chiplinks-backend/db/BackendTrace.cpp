#include "BackendTrace.hpp"

#include <QByteArray>
#include <QMetaType>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <cstdio>

namespace {

int readInitialLevel() {

    const QByteArray env = qgetenv("CHIPLINKS_TRACE");
    if (env.isEmpty()) return 0;
    bool ok = false;
    const int v = QString::fromLatin1(env).toInt(&ok);
    return ok ? v : 0;
}

std::atomic<int> g_level{readInitialLevel()};

QString clip(const QString &s, int max) {
    return s.size() <= max ? s : s.left(max) + QStringLiteral("..");
}

}

int  BackendTrace::level()           { return g_level; }
void BackendTrace::setLevel(int lvl) { g_level = lvl; }

QString BackendTrace::brief(const QVariant &v) {
    if (!v.isValid() || v.isNull()) return QStringLiteral("null");

    if (v.typeId() == QMetaType::QVariantList) {
        return QStringLiteral("[%1]").arg(v.toList().size());
    }
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        QString out = QStringLiteral("{");
        int i = 0;
        for (auto it = m.constBegin(); it != m.constEnd() && i < 8; ++it, ++i) {
            if (i) out += QStringLiteral(", ");
            const QVariant &val = it.value();
            QString vs;
            if (val.typeId() == QMetaType::QVariantList)     vs = QStringLiteral("[%1]").arg(val.toList().size());
            else if (val.typeId() == QMetaType::QVariantMap) vs = QStringLiteral("{..}");
            else                                             vs = clip(val.toString(), 40);
            out += it.key() + QStringLiteral("=") + vs;
        }
        if (m.size() > i) out += QStringLiteral(", +%1").arg(m.size() - i);
        out += QStringLiteral("}");
        return clip(out, 200);
    }
    return clip(v.toString(), 100);
}

void BackendTrace::in(int lvl, const char *method, const QString &detail) {
    std::fprintf(stderr, "[chiplinks-trace] >> %s %s(%s)\n",
                 lvl >= 2 ? "rd " : "cmd", method, detail.toUtf8().constData());
}

void BackendTrace::out(const char *signalName, const QString &detail) {
    std::fprintf(stderr, "[chiplinks-trace] << sig %s(%s)\n",
                 signalName, detail.toUtf8().constData());
}
