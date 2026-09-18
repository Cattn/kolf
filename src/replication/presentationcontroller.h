// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <deque>

namespace Kolf::Replication {
class PresentationController : public QObject {
    Q_OBJECT
public:
    explicit PresentationController(QObject *parent = nullptr);
    void setDelay(int milliseconds);
    void push(const QJsonObject &state, double hostMs);
    void clear();
Q_SIGNALS:
    void present(const QJsonObject &state);
private:
    struct Sample { QJsonObject state; double time; };
    std::deque<Sample> m_samples;
    QElapsedTimer m_clock;
    QTimer m_timer;
    double m_offset = 0;
    int m_delay = 100;
};
}
