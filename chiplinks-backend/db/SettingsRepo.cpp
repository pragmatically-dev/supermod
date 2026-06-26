#include "SettingsRepo.hpp"
#include "Database.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <cstdio>

namespace {

bool bindText(sqlite3_stmt *stmt, int idx, const QString &s) {
    const QByteArray utf8 = s.toUtf8();
    return sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

void logErr(sqlite3 *db, const char *op) {
    std::fprintf(stderr, "[chiplinks-backend] %s failed: %s\n", op,
                 db ? sqlite3_errmsg(db) : "(no db)");
}

}

const std::vector<SettingsRepo::Def> &SettingsRepo::schema() {
    static const std::vector<Def> kSchema = {

        { "pin.dotSize",        "chiplinks", "number", "Pin dot size",
          "Diameter of a collapsed chiplink pin on the page.",
          "24",         8,    48,     1,    0, nullptr },
        { "pin.pillHeight",     "chiplinks", "number", "Pin pill height",
          "Height of the expanded pin pill that shows the link name.",
          "44",        24,    80,     2,    0, nullptr },
        { "pin.moveHoldMs",     "chiplinks", "number", "Hold to move",
          "How long to keep holding a pin before it becomes draggable, in milliseconds.",
          "500",      200,  1500,    50,    0, nullptr },
        { "pin.defaultX",       "chiplinks", "number", "Default position X",
          "Horizontal placement of a new pin, as a fraction of the page width.",
          "0.42",     0.05,  0.95, 0.01,    2, nullptr },
        { "pin.defaultY",       "chiplinks", "number", "Default position Y",
          "Vertical placement of a new pin, in scene pixels from the page top.",
          "988",        0,  4000,    20,    0, nullptr },
        { "link.tapToGo",       "chiplinks", "enum",   "Tap-to-go in Recent links",
          "When on, tapping a recent link navigates to it instead of placing a new chiplink.",
          "off",        0,     0,     0,    0, "off,on" },
        { "link.backlinkPreview","chiplinks", "enum",  "Backlink page previews",
          "When on, an expanded backlink folder shows a thumbnail of each source page.",
          "off",        0,     0,     0,    0, "off,on" },

        { "postit.chipWidth",   "postit",    "number", "Chip width",
          "Base width of a post-it chip on the page.",
          "110",       60,   240,    10,    0, nullptr },
        { "postit.chipHeight",  "postit",    "number", "Chip height",
          "Base height of a post-it chip on the page.",
          "92",        50,   200,    10,    0, nullptr },
        { "postit.scaleDefault","postit",    "number", "Default scale",
          "Size multiplier applied to a freshly created post-it.",
          "1.0",      0.6,   3.0,  0.25,    2, nullptr },
        { "postit.scaleMin",    "postit",    "number", "Minimum scale",
          "Smallest size the [-] button can shrink a post-it to.",
          "0.6",      0.3,   1.5,   0.1,    1, nullptr },
        { "postit.scaleMax",    "postit",    "number", "Maximum scale",
          "Largest size the [+] button can grow a post-it to.",
          "3.0",      1.5,   6.0,  0.25,    2, nullptr },
        { "postit.scaleStep",   "postit",    "number", "Scale step",
          "How much each tap of [-] / [+] changes the post-it size.",
          "0.25",     0.1,   1.0,  0.05,    2, nullptr },
        { "postit.editorSizeFrac","postit",  "number", "Editor canvas size",
          "Size of the writing pop-up, as a fraction of the screen.",
          "0.95",     0.5,   1.0,  0.05,    2, nullptr },
        { "postit.longPressMs", "postit",    "number", "Long-press",
          "Hold time to enter chip edit mode, in milliseconds.",
          "500",      200,  1500,    50,    0, nullptr },
        { "postit.fabCorner",   "postit",    "enum",   "+Post-it button corner",
          "Which corner of the screen the +Post-it button sits in.",
          "bottom-right", 0, 0,      0,    0,
          "bottom-right,bottom-left,top-right,top-left" },

        { "graph.labelFade",    "graph",     "number", "Label fade",
          "Zoom level at which node labels appear.",
          "0.75",     0.5,   3.0,  0.25,    2, nullptr },
        { "graph.nodeSize",     "graph",     "number", "Node size",
          "Node radius multiplier.",
          "1.0",      0.3,   3.0,   0.1,    1, nullptr },
        { "graph.edgeWidth",    "graph",     "number", "Link thickness",
          "Edge line width in pixels.",
          "1",          1,     6,     1,    0, nullptr },
        { "graph.gravity",      "graph",     "number", "Center force",
          "Pull of nodes toward the center of the graph.",
          "0.3",      0.0,   1.0,   0.1,    1, nullptr },
        { "graph.repulsion",    "graph",     "number", "Repel force",
          "How strongly nodes push each other apart.",
          "1.0",      0.2,   3.0,   0.2,    1, nullptr },
        { "graph.linkForce",    "graph",     "number", "Link force",
          "How strongly linked nodes attract each other.",
          "1.0",      0.2,   3.0,   0.2,    1, nullptr },
        { "graph.linkDist",     "graph",     "number", "Link distance",
          "Ideal length of an edge between linked nodes.",
          "1.0",      0.3,   3.0,   0.2,    1, nullptr },
    };
    return kSchema;
}

const SettingsRepo::Def *SettingsRepo::defFor(const QString &key) {
    for (const Def &d : schema())
        if (key == QLatin1String(d.key)) return &d;
    return nullptr;
}

QStringList SettingsRepo::choiceList(const Def *d) {
    if (!d || !d->choices) return {};
    return QString::fromLatin1(d->choices).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

QString SettingsRepo::rawGet(const QString &key) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return QString();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT value FROM settings WHERE key = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "SettingsRepo::rawGet prepare");
        return QString();
    }
    bindText(stmt, 1, key);
    QString result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (txt) result = QString::fromUtf8(txt);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool SettingsRepo::rawSet(const QString &key, const QString &section, const QString &value) {
    sqlite3 *handle = m_db.handle();
    if (!handle) return false;
    static const char *kSql = R"sql(
        INSERT INTO settings (key, section, value) VALUES (?, ?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value;
    )sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "SettingsRepo::rawSet prepare");
        return false;
    }
    bindText(stmt, 1, key);
    bindText(stmt, 2, section);
    bindText(stmt, 3, value);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "SettingsRepo::rawSet step");
    sqlite3_finalize(stmt);
    return ok;
}

QVariantMap SettingsRepo::loadSection(const QString &section) {
    QVariantMap out;
    for (const Def &d : schema()) {
        if (section != QLatin1String(d.section)) continue;
        const QString stored = rawGet(QString::fromLatin1(d.key));
        if (QLatin1String(d.type) == QLatin1String("enum")) {
            QString v = stored.isNull() ? QString::fromLatin1(d.def) : stored;
            if (!choiceList(&d).contains(v)) v = QString::fromLatin1(d.def);
            out.insert(QString::fromLatin1(d.key), v);
        } else {
            double v = (stored.isNull() ? QString::fromLatin1(d.def) : stored).toDouble();
            v = std::max(d.min, std::min(d.max, v));
            out.insert(QString::fromLatin1(d.key), v);
        }
    }
    return out;
}

QVariantList SettingsRepo::schemaFor(const QString &section) {
    QVariantList out;
    for (const Def &d : schema()) {
        if (section != QLatin1String(d.section)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("key"),    QString::fromLatin1(d.key));
        m.insert(QStringLiteral("label"),  QString::fromLatin1(d.label));
        m.insert(QStringLiteral("desc"),   QString::fromLatin1(d.desc ? d.desc : ""));
        m.insert(QStringLiteral("type"),   QString::fromLatin1(d.type));
        m.insert(QStringLiteral("min"),    d.min);
        m.insert(QStringLiteral("max"),    d.max);
        m.insert(QStringLiteral("step"),   d.step);
        m.insert(QStringLiteral("dec"),    d.dec);
        const QStringList ch = choiceList(&d);
        m.insert(QStringLiteral("choices"), ch);
        const QString stored = rawGet(QString::fromLatin1(d.key));
        if (QLatin1String(d.type) == QLatin1String("enum")) {
            QString v = stored.isNull() ? QString::fromLatin1(d.def) : stored;
            if (!ch.contains(v)) v = QString::fromLatin1(d.def);
            m.insert(QStringLiteral("value"), v);
        } else {
            double v = (stored.isNull() ? QString::fromLatin1(d.def) : stored).toDouble();
            v = std::max(d.min, std::min(d.max, v));
            m.insert(QStringLiteral("value"), v);
        }
        out.append(m);
    }
    return out;
}

QString SettingsRepo::setClamped(const QString &key, const QString &value) {
    const Def *d = defFor(key);
    if (!d) return QString();
    QString toStore;
    if (QLatin1String(d->type) == QLatin1String("enum")) {
        if (!choiceList(d).contains(value)) return QString();
        toStore = value;
    } else {
        double v = std::max(d->min, std::min(d->max, value.toDouble()));

        toStore = QString::number(v, 'f', d->dec);
    }
    if (!rawSet(key, QString::fromLatin1(d->section), toStore)) return QString();
    return QString::fromLatin1(d->section);
}

QString SettingsRepo::resetSection(const QString &section) {

    bool known = false;
    for (const Def &d : schema())
        if (section == QLatin1String(d.section)) { known = true; break; }
    if (!known) return QString();

    sqlite3 *handle = m_db.handle();
    if (!handle) return QString();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "DELETE FROM settings WHERE section = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        logErr(handle, "SettingsRepo::resetSection prepare");
        return QString();
    }
    bindText(stmt, 1, section);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) logErr(handle, "SettingsRepo::resetSection step");
    sqlite3_finalize(stmt);
    return ok ? section : QString();
}
