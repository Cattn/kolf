// SPDX-License-Identifier: GPL-2.0-or-later
#include "settingsdialog.h"

#include "online/playerprofileeditor.h"
#include "online/serversettings.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

using namespace Kolf::Ui;

namespace {
QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setContentsMargins(24, 0, 0, 6);
    return label;
}

KPageWidgetItem *addSettingsPage(KPageDialog *dialog, QWidget *page, const QString &name,
    const QString &header, const QString &icon)
{
    auto *item = dialog->addPage(page, name);
    item->setHeader(header);
    item->setIcon(QIcon::fromTheme(icon));
    return item;
}
}

SettingsDialog::SettingsDialog(const Preferences &preferences, QWidget *parent)
    : KPageDialog(parent)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(i18nc("@title:window", "Configure Kolf"));
    setFaceType(KPageDialog::List);
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *gameplay = new QWidget(this);
    auto *gameplayLayout = new QVBoxLayout(gameplay);
    auto *puttingGroup = new QGroupBox(i18n("Putting style"), gameplay);
    auto *puttingLayout = new QVBoxLayout(puttingGroup);
    m_simplePutting = new QRadioButton(i18n("Simple"), puttingGroup);
    m_simplePutting->setObjectName(QStringLiteral("settingsSimplePutting"));
    puttingLayout->addWidget(m_simplePutting);
    puttingLayout->addWidget(hint(i18n("Hold the mouse button or the Down arrow to build power, then release to putt."), puttingGroup));
    m_advancedPutting = new QRadioButton(i18n("Advanced"), puttingGroup);
    m_advancedPutting->setObjectName(QStringLiteral("settingsAdvancedPutting"));
    puttingLayout->addWidget(m_advancedPutting);
    puttingLayout->addWidget(hint(i18n("Press once to start, again to set power, then a third time as the meter swings through the middle. A badly timed third press sends the ball off line."), puttingGroup));
    gameplayLayout->addWidget(puttingGroup);
    m_useMouse = new QCheckBox(i18n("Aim the putter with the mouse"), gameplay);
    gameplayLayout->addWidget(m_useMouse);
    gameplayLayout->addWidget(hint(i18n("When off, aim with the Left and Right arrow keys."), gameplay));
    m_confirmLeave = new QCheckBox(i18n("Ask before leaving a game for the main menu"), gameplay);
    gameplayLayout->addWidget(m_confirmLeave);
    auto *resetMessages = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-information")),
        i18n("Show All Hidden Messages Again"), gameplay);
    resetMessages->setObjectName(QStringLiteral("settingsResetMessages"));
    resetMessages->setToolTip(i18n("Messages you turned off with \"Do not ask again\" will appear again."));
    auto *resetRow = new QHBoxLayout;
    resetRow->addWidget(resetMessages);
    resetRow->addStretch();
    gameplayLayout->addLayout(resetRow);
    gameplayLayout->addStretch();
    addSettingsPage(this, gameplay, i18n("Gameplay"), i18n("How you aim and putt"), QStringLiteral("games-config-options"));

    auto *display = new QWidget(this);
    auto *displayLayout = new QVBoxLayout(display);
    m_showInfo = new QCheckBox(i18n("Show slope arrows and object information (Ctrl+I)"), display);
    displayLayout->addWidget(m_showInfo);
    displayLayout->addWidget(hint(i18n("Arrows point downhill; longer arrows are steeper. Also shows where black holes send the ball."), display));
    m_showGuideLine = new QCheckBox(i18n("Show the putter guideline"), display);
    displayLayout->addWidget(m_showGuideLine);
    displayLayout->addWidget(hint(i18n("A line from the ball in the direction you are aiming."), display));
    displayLayout->addStretch();
    addSettingsPage(this, display, i18n("Display"), i18n("What appears on the course"), QStringLiteral("preferences-desktop-display"));

    auto *sound = new QWidget(this);
    auto *soundLayout = new QVBoxLayout(sound);
    m_sound = new QCheckBox(i18n("Play sounds"), sound);
    soundLayout->addWidget(m_sound);
    soundLayout->addStretch();
    addSettingsPage(this, sound, i18n("Sound"), i18n("Sound effects"), QStringLiteral("preferences-desktop-sound"));

    auto *online = new QWidget(this);
    auto *onlineLayout = new QVBoxLayout(online);
    auto *profileGroup = new QGroupBox(i18n("Your online player"), online);
    auto *profileLayout = new QVBoxLayout(profileGroup);
    m_profile = new Online::PlayerProfileEditor(profileGroup);
    profileLayout->addWidget(m_profile);
    onlineLayout->addWidget(profileGroup);
    m_confirmHostActions = new QCheckBox(i18n("Ask before host actions that change scores"), online);
    onlineLayout->addWidget(m_confirmHostActions);
    onlineLayout->addWidget(hint(i18n("Reset Hole, Skip Hole, moving to another hole, and approving a player's request."), online));
    onlineLayout->addStretch();
    addSettingsPage(this, online, i18n("Online"), i18n("Playing with friends over the internet"), QStringLiteral("network-connect"));

    m_servers = new Online::ServerSettingsPage(preferences.servers, this);
    addSettingsPage(this, m_servers, i18n("Servers"), i18n("Where online games are hosted"), QStringLiteral("network-server"));

    (preferences.advancedPutting ? m_advancedPutting : m_simplePutting)->setChecked(true);
    m_useMouse->setChecked(preferences.useMouse);
    m_confirmLeave->setChecked(preferences.confirmLeaveGame);
    m_showInfo->setChecked(preferences.showInfo);
    m_showGuideLine->setChecked(preferences.showGuideLine);
    m_sound->setChecked(preferences.sound);
    m_confirmHostActions->setChecked(preferences.confirmHostActions);
    m_profile->setProfile(preferences.onlineName, preferences.onlineColorMode, preferences.onlineCustomColor);

    connect(resetMessages, &QPushButton::clicked, this, [this, resetMessages, confirmLeave = m_confirmLeave] {
        confirmLeave->setChecked(true);
        resetMessages->setEnabled(false);
        Q_EMIT resetMessagesRequested();
    });
    connect(buttonBox(), &QDialogButtonBox::accepted, this, [this] {
        if (!onlineProfileValid()) {
            m_profile->setFocus();
            return;
        }
        accept();
    });
    disconnect(buttonBox(), &QDialogButtonBox::accepted, this, &QDialog::accept);
    resize(640, 460);
}

bool SettingsDialog::onlineProfileValid() const
{
    return m_profile->isValid();
}

Preferences SettingsDialog::preferences() const
{
    Preferences result;
    result.advancedPutting = m_advancedPutting->isChecked();
    result.useMouse = m_useMouse->isChecked();
    result.confirmLeaveGame = m_confirmLeave->isChecked();
    result.showInfo = m_showInfo->isChecked();
    result.showGuideLine = m_showGuideLine->isChecked();
    result.sound = m_sound->isChecked();
    result.confirmHostActions = m_confirmHostActions->isChecked();
    result.onlineName = m_profile->playerName();
    result.onlineColorMode = m_profile->colorMode();
    result.onlineCustomColor = m_profile->customColor();
    result.servers = m_servers->serverList();
    return result;
}
