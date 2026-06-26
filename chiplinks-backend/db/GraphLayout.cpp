#include "GraphLayout.hpp"

#include <QHash>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double kTwoPi = 6.283185307179586;

double param(const QVariantMap &p, const char *key, double def) {
    const auto it = p.find(QString::fromLatin1(key));
    return it == p.end() ? def : it.value().toDouble();
}
}

QVariantList GraphLayout::layout(const QVariantList &nodesIn,
                                 const QVariantList &edges,
                                 const QVariantMap &params) {
    const int N = nodesIn.size();
    QVariantList out;
    out.reserve(N);

    std::vector<QVariantMap> nodes;
    nodes.reserve(N);
    QHash<QString, int> idx;
    for (int i = 0; i < N; ++i) {
        QVariantMap n = nodesIn[i].toMap();
        idx.insert(n.value(QStringLiteral("id")).toString(), i);
        nodes.push_back(std::move(n));
    }

    const double inset = 0.06;
    if (N == 0) return out;
    if (N == 1) {
        nodes[0][QStringLiteral("x")] = 0.5;
        nodes[0][QStringLiteral("y")] = 0.5;
        out.append(nodes[0]);
        return out;
    }

    const double gravity   = param(params, "gravity",   0.3);
    const double repulsion = param(params, "repulsion", 1.0);
    const double linkForce = param(params, "linkForce", 1.0);
    const double linkDist  = param(params, "linkDist",  1.0);

    std::vector<double> px(N), py(N);
    const double k = std::sqrt(1.0 / N) * linkDist;
    const double R = 0.4;
    for (int i = 0; i < N; ++i) {
        const double a = (kTwoPi * i) / N;
        px[i] = 0.5 + R * std::cos(a);
        py[i] = 0.5 + R * std::sin(a);
    }

    std::vector<int> E;
    E.reserve(static_cast<size_t>(edges.size()) * 2);
    for (const QVariant &ev : edges) {
        const QVariantMap e = ev.toMap();
        const auto si = idx.find(e.value(QStringLiteral("s")).toString());
        const auto ti = idx.find(e.value(QStringLiteral("t")).toString());
        if (si == idx.end() || ti == idx.end() || si.value() == ti.value()) continue;
        E.push_back(si.value());
        E.push_back(ti.value());
    }

    const int iters = std::max(6, std::min(30, static_cast<int>(std::lround(600.0 / N))));
    double t = 0.1;
    const double cool = t / (iters + 1);
    std::vector<double> dx(N), dy(N);

    for (int it = 0; it < iters; ++it) {
        for (int i = 0; i < N; ++i) { dx[i] = 0.0; dy[i] = 0.0; }

        for (int i = 0; i < N; ++i) {
            for (int j = i + 1; j < N; ++j) {
                double ex = px[i] - px[j];
                double ey = py[i] - py[j];
                double d2 = ex * ex + ey * ey;
                if (d2 < 1e-6) { ex = 1e-3 * (i + 1); ey = 1e-3 * (j + 1); d2 = ex * ex + ey * ey; }
                const double d = std::sqrt(d2);
                const double fr = (k * k) / d * repulsion;
                const double ux = ex / d, uy = ey / d;
                dx[i] += ux * fr; dy[i] += uy * fr;
                dx[j] -= ux * fr; dy[j] -= uy * fr;
            }
        }

        for (size_t p = 0; p + 1 < E.size(); p += 2) {
            const int u = E[p], v = E[p + 1];
            const double axx = px[u] - px[v];
            const double ayy = py[u] - py[v];
            double ad = std::sqrt(axx * axx + ayy * ayy);
            if (ad < 1e-3) ad = 1e-3;
            const double fa = (ad * ad) / k * linkForce;
            const double aux = axx / ad, auy = ayy / ad;
            dx[u] -= aux * fa; dy[u] -= auy * fa;
            dx[v] += aux * fa; dy[v] += auy * fa;
        }

        if (gravity > 0.0) {
            for (int i = 0; i < N; ++i) {
                dx[i] += (0.5 - px[i]) * gravity;
                dy[i] += (0.5 - py[i]) * gravity;
            }
        }

        for (int i = 0; i < N; ++i) {
            double dl = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i]);
            if (dl < 1e-6) dl = 1e-6;
            const double step = std::min(dl, t);
            px[i] += (dx[i] / dl) * step;
            py[i] += (dy[i] / dl) * step;
            if (px[i] < 0.0) px[i] = 0.0; else if (px[i] > 1.0) px[i] = 1.0;
            if (py[i] < 0.0) py[i] = 0.0; else if (py[i] > 1.0) py[i] = 1.0;
        }
        t -= cool;
    }

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (int i = 0; i < N; ++i) {
        minX = std::min(minX, px[i]); maxX = std::max(maxX, px[i]);
        minY = std::min(minY, py[i]); maxY = std::max(maxY, py[i]);
    }
    double spanX = maxX - minX; if (spanX <= 0.0) spanX = 1.0;
    double spanY = maxY - minY; if (spanY <= 0.0) spanY = 1.0;
    const double range = 1.0 - 2.0 * inset;
    for (int i = 0; i < N; ++i) {
        nodes[i][QStringLiteral("x")] = inset + ((px[i] - minX) / spanX) * range;
        nodes[i][QStringLiteral("y")] = inset + ((py[i] - minY) / spanY) * range;
        out.append(nodes[i]);
    }
    return out;
}
