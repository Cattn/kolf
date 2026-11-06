// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QGraphicsView;

namespace Kolf::Ui {
class HazardPrompt : public QDialog {
    Q_OBJECT
public:
    enum Choice { Drop, Rehit };
    static Choice ask(QGraphicsView *view, const QPointF &ballScenePos, const QString &playerName);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    HazardPrompt(const QString &playerName, QWidget *parent);
    Choice m_choice = Drop;
    QCheckBox *m_remember;
};
}
