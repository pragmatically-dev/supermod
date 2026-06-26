#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

namespace TocNumbering {

QStringList prefixes(const QVariantList &levels, const QString &style);

}
