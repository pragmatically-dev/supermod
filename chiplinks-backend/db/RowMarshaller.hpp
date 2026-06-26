#pragma once

#include <QString>
#include <QVariantMap>

struct sqlite3_stmt;

class RowMarshaller {
public:
    static QVariantMap toMap(sqlite3_stmt *stmt);
};
