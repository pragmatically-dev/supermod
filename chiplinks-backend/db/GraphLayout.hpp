#pragma once

#include <QVariantList>
#include <QVariantMap>

namespace GraphLayout {

QVariantList layout(const QVariantList &nodes,
                    const QVariantList &edges,
                    const QVariantMap &params);

}
