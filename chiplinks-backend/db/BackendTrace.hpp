#pragma once

#include <QString>
#include <QVariant>

namespace BackendTrace {

int  level();
void setLevel(int lvl);

inline bool on(int lvl) { return level() >= lvl; }

void in(int lvl, const char *method, const QString &detail);

void out(const char *signalName, const QString &detail);

QString brief(const QVariant &v);

}

#define CL_CMD(detail)  do { if (BackendTrace::on(1)) BackendTrace::in(1, __func__, (detail)); } while (0)
#define CL_READ(detail) do { if (BackendTrace::on(2)) BackendTrace::in(2, __func__, (detail)); } while (0)
