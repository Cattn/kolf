// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QFrame>

class QLabel;
class KolfGame;
class QHBoxLayout;
class QToolButton;

namespace Kolf::Ui {
class GameHud : public QFrame {
    Q_OBJECT
public:
    explicit GameHud(QWidget *parent = nullptr);
    void setHole(int hole, int holes, int par);
    void setTurn(const QString &name, const QColor &color, int stroke, bool yourTurn = true, bool markLocalTurn = false);
    void setMessage(const QString &message);
    void showPowerUps(KolfGame *game, bool canUse);
    void handlePowerUpClick(KolfGame *game, int index);
    void setPowerUps(bool enabled, const QStringList &items, const QStringList &statuses, int armed = -1, const QString &armedTarget = {}, bool canUse = true);
    void setEditing(int hole, int holes);
    void setHint(const QString &hint);
    void clearHint();
    void addControl(QWidget *widget);

Q_SIGNALS:
    void hintDismissed();
    void powerUpClicked(int index);

private:
    QHBoxLayout *m_header;
    QLabel *m_hole;
    QLabel *m_swatch;
    QLabel *m_turn;
    QLabel *m_stroke;
    QWidget *m_hintRow;
    QLabel *m_hint;
    QLabel *m_powerUps = nullptr;
    QToolButton *m_itemButtons[2] = {nullptr, nullptr};
    QToolButton *m_dismissHint;
};
}
