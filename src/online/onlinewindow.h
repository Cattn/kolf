// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "onlinecoordinator.h"

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextEdit;

namespace Kolf::Online {
class OnlineWindow : public QWidget {
    Q_OBJECT
public:
    explicit OnlineWindow(QWidget *parent = nullptr);

private:
    void showEntry();
    void showLobby(const QJsonObject &state);
    void showResults(const QJsonObject &state);
    void savePreferences();

    OnlineCoordinator m_coordinator;
    QStackedWidget *m_pages;
    QLineEdit *m_endpoint;
    QLabel *m_connectStatus;
    QLineEdit *m_name;
    QLineEdit *m_color;
    QLineEdit *m_joinCode;
    QComboBox *m_createCourse;
    QLabel *m_entryStatus;
    QLabel *m_lobbySummary;
    QLabel *m_lobbyStatus;
    QListWidget *m_members;
    QComboBox *m_lobbyCourse;
    QPushButton *m_ready;
    QPushButton *m_start;
    QTextEdit *m_result;
    QJsonObject m_state;
};
}
