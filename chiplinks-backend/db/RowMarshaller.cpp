#include "RowMarshaller.hpp"

#include <sqlite3.h>
#include <QByteArray>

QVariantMap RowMarshaller::toMap(sqlite3_stmt *stmt) {
    QVariantMap row;
    if (!stmt) return row;
    const int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
        const char *name = sqlite3_column_name(stmt, i);
        if (!name) continue;
        const QString key = QString::fromUtf8(name);
        switch (sqlite3_column_type(stmt, i)) {
        case SQLITE_INTEGER:
            row.insert(key, static_cast<qlonglong>(sqlite3_column_int64(stmt, i)));
            break;
        case SQLITE_FLOAT:
            row.insert(key, sqlite3_column_double(stmt, i));
            break;
        case SQLITE_TEXT: {
            const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
            row.insert(key, QString::fromUtf8(txt ? txt : ""));
            break;
        }
        case SQLITE_BLOB: {
            const void *blob = sqlite3_column_blob(stmt, i);
            const int n = sqlite3_column_bytes(stmt, i);
            row.insert(key, QByteArray(reinterpret_cast<const char *>(blob), n));
            break;
        }
        case SQLITE_NULL:
        default:
            row.insert(key, QVariant());
            break;
        }
    }
    return row;
}
