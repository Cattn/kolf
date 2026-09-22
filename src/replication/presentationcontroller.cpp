// SPDX-License-Identifier: GPL-2.0-or-later
#include "presentationcontroller.h"
#include <QJsonArray>
#include <cmath>

using namespace Kolf::Replication;
namespace {
QJsonArray blend(const QJsonArray &a, const QJsonArray &b, double t) {
    if (a.size() != b.size()) return b;
    QJsonArray result;
    for (int i = 0; i < b.size(); ++i) {
        const auto from = a[i].toObject(); auto to = b[i].toObject();
        // State/visibility jumps, teleports and corrections are discontinuities.
        if (from[QStringLiteral("id")] == to[QStringLiteral("id")]
            && from[QStringLiteral("visible")] == to[QStringLiteral("visible")]
            && from[QStringLiteral("state")] == to[QStringLiteral("state")]
            // Stopped and holed balls must use the exact authoritative point.
            && (!to.contains(QStringLiteral("state")) || to[QStringLiteral("state")].toInt() == 0)
            && std::hypot(to[QStringLiteral("x")].toDouble() - from[QStringLiteral("x")].toDouble(),
                          to[QStringLiteral("y")].toDouble() - from[QStringLiteral("y")].toDouble()) < 50) {
            for (const auto *key : {"x", "y"}) {
                const auto k = QLatin1String(key); to[k] = from[k].toDouble() + (to[k].toDouble() - from[k].toDouble()) * t;
            }
            if (from[QStringLiteral("line")].isArray() && to[QStringLiteral("line")].isArray()) {
                auto first = from[QStringLiteral("line")].toArray(), last = to[QStringLiteral("line")].toArray();
                if (first.size() == 4 && last.size() == 4) for (int j = 0; j < 4; ++j) last[j] = first[j].toDouble() + (last[j].toDouble() - first[j].toDouble()) * t;
                to[QStringLiteral("line")] = last;
            }
        }
        result.append(to);
    }
    return result;
}
}
PresentationController::PresentationController(QObject *parent) : QObject(parent) {
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_samples.empty()) return;
        const double target = double(m_clock.elapsed()) - m_offset - m_delay;
        while (m_samples.size() > 2 && m_samples[1].time <= target) m_samples.pop_front();
        auto state = m_samples.front().state;
        if (m_samples.size() >= 2 && target > m_samples.front().time) {
            const auto &a = m_samples[0], &b = m_samples[1];
            const double t = qBound(0.0, (target - a.time) / qMax(1.0, b.time - a.time), 1.0);
            state = b.state;
            if (a.state[QStringLiteral("holeGeneration")] == b.state[QStringLiteral("holeGeneration")]
                && a.state[QStringLiteral("stateRevision")] == b.state[QStringLiteral("stateRevision")]) {
                state[QStringLiteral("balls")] = blend(a.state[QStringLiteral("balls")].toArray(), b.state[QStringLiteral("balls")].toArray(), t);
                state[QStringLiteral("objects")] = blend(a.state[QStringLiteral("objects")].toArray(), b.state[QStringLiteral("objects")].toArray(), t);
            }
        }
        Q_EMIT present(state); // No extrapolation beyond the latest received sample.
    });
    m_timer.start(16);
}
void PresentationController::setDelay(int ms) { m_delay = qBound(0, ms, 500); }
void PresentationController::clear() { m_samples.clear(); }
void PresentationController::push(const QJsonObject &state, double hostMs) {
    if (!std::isfinite(hostMs)) return;
    if (!m_samples.empty() && hostMs <= m_samples.back().time) return;
    if (m_samples.empty()) m_offset = double(m_clock.elapsed()) - hostMs;
    m_samples.push_back({state, hostMs});
    while (m_samples.size() > 16) m_samples.pop_front();
    if (!m_delay) Q_EMIT present(state);
}
