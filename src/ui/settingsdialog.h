// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "online/serverlist.h"

#include <KPageDialog>

class QCheckBox;
class QRadioButton;
namespace Kolf::Online { class PlayerProfileEditor; class ServerSettingsPage; }

namespace Kolf::Ui {
struct Preferences {
    bool advancedPutting = false;
    bool useMouse = true;
    bool confirmLeaveGame = true;
    bool showInfo = true;
    bool showGuideLine = true;
    bool sound = true;
    bool confirmHostActions = true;
    QString onlineName;
    QString onlineColorMode;
    QString onlineCustomColor;
    Online::ServerList servers;
};

class SettingsDialog : public KPageDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const Preferences &preferences, QWidget *parent = nullptr);
    Preferences preferences() const;
    bool onlineProfileValid() const;
    void showServersPage();

Q_SIGNALS:
    void resetMessagesRequested();

private:
    QRadioButton *m_simplePutting;
    QRadioButton *m_advancedPutting;
    QCheckBox *m_useMouse;
    QCheckBox *m_confirmLeave;
    QCheckBox *m_showInfo;
    QCheckBox *m_showGuideLine;
    QCheckBox *m_sound;
    QCheckBox *m_confirmHostActions;
    Online::PlayerProfileEditor *m_profile;
    Online::ServerSettingsPage *m_servers;
    KPageWidgetItem *m_serversItem;
};
}
