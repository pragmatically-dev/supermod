#include "TocNumbering.hpp"

#include <QChar>
#include <QHash>

namespace {

QString toRoman(int num) {
    if (num > 3999 || num <= 0) return QString::number(num);
    static const int   vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *syms[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    QString result;
    for (int i = 0; i < 13; ++i) {
        while (num >= vals[i]) { result += QLatin1String(syms[i]); num -= vals[i]; }
    }
    return result;
}

QString formatNumber(int num, int level, const QString &style) {
    if (style == QLatin1String("arabic")) return QString::number(num) + QStringLiteral(". ");
    if (style == QLatin1String("outline")) {
        switch (level) {
            case 0: return toRoman(num) + QStringLiteral(". ");
            case 1: return QString(QChar(64 + num)) + QStringLiteral(". ");
            case 2: return QString::number(num) + QStringLiteral(". ");
            case 3: return QString(QChar(96 + num)) + QStringLiteral(". ");
            case 4: return QStringLiteral("(") + QString::number(num) + QStringLiteral(") ");
            case 5: return QStringLiteral("(") + QString(QChar(96 + num)) + QStringLiteral(") ");
            default: return QString::number(num) + QStringLiteral(". ");
        }
    }
    return QString();
}

}

QStringList TocNumbering::prefixes(const QVariantList &levels, const QString &style) {
    QStringList out;
    if (style.isEmpty()) return out;
    out.reserve(levels.size());

    QHash<int, int> counters;
    for (const QVariant &lv : levels) {
        const int level = lv.toInt();
        for (auto it = counters.begin(); it != counters.end();) {
            if (it.key() > level) it = counters.erase(it);
            else ++it;
        }
        counters[level] = counters.value(level, 0) + 1;

        if (style == QLatin1String("decimal")) {
            QStringList parts;
            for (int l = 0; l <= level; ++l) parts << QString::number(counters.value(l, 1));
            out << parts.join(QLatin1Char('.')) + QStringLiteral(" ");
        } else {
            out << formatNumber(counters.value(level), level, style);
        }
    }
    return out;
}
