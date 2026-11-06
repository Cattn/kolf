// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFrame>
#include <QPointer>

class QCommandLinkButton;

namespace Kolf::Ui {
class HomeView : public QFrame {
    Q_OBJECT
public:
    explicit HomeView(QWidget *parent = nullptr);
    void refresh();
    void setAnchor(QWidget *anchor);
    static QString lastSavedGame();
    static void rememberSavedGame(const QString &path);

Q_SIGNALS:
    void playRequested();
    void onlineRequested();
    void editorRequested();
    void tutorialRequested();
    void continueRequested(const QString &savedGame);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();
    QPointer<QWidget> m_anchor;
    QCommandLinkButton *m_continue;
    QCommandLinkButton *m_play;
    QString m_savedGame;
};
}
