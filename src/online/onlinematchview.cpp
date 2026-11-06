// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinematchview.h"

#include "game.h"
#include "scoreboard.h"
#include "session/sessioncontroller.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KStandardGuiItem>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

using namespace Kolf::Online;
using Kolf::Session::SessionController;

OnlineMatchView::OnlineMatchView(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_scoreboard = new ScoreBoard(this);
    m_layout->addWidget(m_scoreboard);
    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setAccessibleName(i18n("Online match notice"));
    m_layout->addWidget(m_notice);
    m_notice->hide();

    m_hazardPanel = new QWidget(this);
    auto *hazardLayout = new QHBoxLayout(m_hazardPanel);
    hazardLayout->setContentsMargins(6, 3, 6, 3);
    hazardLayout->addWidget(new QLabel(i18n("Your ball is in a hazard. Choose how to continue:"), m_hazardPanel));
    hazardLayout->addStretch();
    m_dropButton = new QPushButton(i18nc("@action:button", "Drop Outside Hazard"), m_hazardPanel);
    m_rehitButton = new QPushButton(i18nc("@action:button", "Rehit"), m_hazardPanel);
    hazardLayout->addWidget(m_dropButton);
    hazardLayout->addWidget(m_rehitButton);
    m_layout->addWidget(m_hazardPanel);
    m_hazardPanel->hide();

    auto *controlsGroup = new QGroupBox(i18n("Match controls"), this);
    auto *controlsLayout = new QVBoxLayout(controlsGroup);
    controlsLayout->setContentsMargins(8, 6, 8, 6);
    m_hostOptions = new QWidget(controlsGroup);
    auto *hostOptions = new QHBoxLayout(m_hostOptions);
    hostOptions->setContentsMargins(0, 0, 0, 0);
    m_hostControls = new QCheckBox(i18nc("@option:check", "Enable Host Controls"), m_hostOptions);
    m_hostControls->setEnabled(false);
    hostOptions->addWidget(m_hostControls);
    m_allowVoting = new QCheckBox(i18nc("@option:check", "Allow Voting"), m_hostOptions);
    m_allowVoting->setChecked(true);
    m_allowVoting->setEnabled(false);
    hostOptions->addWidget(m_allowVoting);
    m_confirmHostActions = new QCheckBox(i18nc("@option:check", "Confirm Host Actions"), m_hostOptions);
    m_confirmHostActions->setAccessibleName(i18n("Confirm host actions before applying them"));
    m_confirmHostActions->setToolTip(i18n("Ask before Reset, Skip, Go, or approving a proposal. This preference only affects this computer."));
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    m_confirmHostActions->setChecked(onlineConfig.readEntry("confirmHostActions", true));
    hostOptions->addWidget(m_confirmHostActions);
    hostOptions->addStretch();
    controlsLayout->addWidget(m_hostOptions);
    connect(m_confirmHostActions, &QCheckBox::toggled, this, [](bool enabled) {
        KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("Online"));
        config.writeEntry("confirmHostActions", enabled);
        config.sync();
    });
    m_guestStatus = new QLabel(controlsGroup);
    m_guestStatus->setWordWrap(true);
    m_guestStatus->setAccessibleName(i18n("Host controls and voting status"));
    controlsLayout->addWidget(m_guestStatus);
    m_guestStatus->hide();

    m_proposalComposer = new QWidget(controlsGroup);
    auto *proposalActions = new QHBoxLayout(m_proposalComposer);
    proposalActions->setContentsMargins(0, 0, 0, 0);
    m_proposalAction = new QComboBox(m_proposalComposer);
    m_proposalAction->setAccessibleName(i18n("Proposed host action"));
    m_proposalAction->setEnabled(false);
    m_proposalAction->addItem(i18n("Undo Shot"), QStringLiteral("undoShot"));
    m_proposalAction->addItem(i18n("Reset Hole"), QStringLiteral("resetHole"));
    m_proposalAction->addItem(i18n("Skip Hole"), QStringLiteral("skipHole"));
    m_proposalAction->addItem(i18n("Go Next"), QStringLiteral("goNext"));
    m_proposalAction->addItem(i18n("Go Previous"), QStringLiteral("goPrevious"));
    m_proposalAction->addItem(i18n("Go First"), QStringLiteral("goFirst"));
    m_proposalAction->addItem(i18n("Go Last"), QStringLiteral("goLast"));
    m_proposalAction->addItem(i18n("Go Random"), QStringLiteral("goRandom"));
    m_proposalAction->addItem(i18n("Switch to Hole"), QStringLiteral("goToHole"));
    m_proposalTarget = new QSpinBox(m_proposalComposer);
    m_proposalTarget->setAccessibleName(i18n("Proposal target hole"));
    m_proposalTarget->setRange(1, 1000);
    m_proposalTarget->setVisible(false);
    m_proposalTarget->setEnabled(false);
    m_proposeButton = new QPushButton(i18nc("@action:button", "Send Proposal"), m_proposalComposer);
    m_proposeButton->setEnabled(false);
    proposalActions->addWidget(m_proposalAction);
    proposalActions->addWidget(m_proposalTarget);
    proposalActions->addWidget(m_proposeButton);
    proposalActions->addStretch();
    controlsLayout->addWidget(m_proposalComposer);
    m_proposalComposer->hide();

    m_proposalLabel = new QLabel(i18n("No active host proposal."), controlsGroup);
    m_proposalLabel->setTextFormat(Qt::PlainText);
    m_proposalLabel->setWordWrap(true);
    m_proposalLabel->setAccessibleName(i18n("Host proposal and vote tally"));
    controlsLayout->addWidget(m_proposalLabel);
    m_proposalLabel->hide();
    m_proposalVotes = new QWidget(controlsGroup);
    auto *proposalVotes = new QHBoxLayout(m_proposalVotes);
    proposalVotes->setContentsMargins(0, 0, 0, 0);
    m_voteYesButton = new QPushButton(i18nc("@action:button", "Vote Yes"), m_proposalVotes);
    m_voteNoButton = new QPushButton(i18nc("@action:button", "Vote No"), m_proposalVotes);
    m_approveButton = new QPushButton(i18nc("@action:button", "Approve"), m_proposalVotes);
    m_rejectButton = new QPushButton(i18nc("@action:button", "Reject"), m_proposalVotes);
    m_withdrawButton = new QPushButton(i18nc("@action:button", "Withdraw"), m_proposalVotes);
    for (auto *button : {m_voteYesButton, m_voteNoButton, m_approveButton, m_rejectButton, m_withdrawButton}) {
        button->setEnabled(false);
        proposalVotes->addWidget(button);
    }
    proposalVotes->addStretch();
    controlsLayout->addWidget(m_proposalVotes);
    m_proposalVotes->hide();
    m_layout->addWidget(controlsGroup);

    auto *navigation = new QHBoxLayout;
    auto *changeServerButton = new QPushButton(i18nc("@action:button", "Change Server"), this);
    auto *leaveOnlineButton = new QPushButton(i18nc("@action:button", "Leave Online"), this);
    navigation->addStretch();
    navigation->addWidget(changeServerButton);
    navigation->addWidget(leaveOnlineButton);
    m_layout->addLayout(navigation);

    connect(m_proposalAction, &QComboBox::currentIndexChanged, this, [this] {
        m_proposalTarget->setVisible(m_proposalAction->currentData().toString() == QLatin1String("goToHole"));
    });
    connect(changeServerButton, &QPushButton::clicked, this, [this] {
        if (KMessageBox::warningTwoActions(this,
            i18n("Leaving this online match will interrupt it for everyone. Change server?"),
            i18nc("@title:window", "Change Server?"),
            KGuiItem(i18nc("@action:button", "Change Server")), KStandardGuiItem::cancel())
            == KMessageBox::PrimaryAction)
            Q_EMIT changeServerRequested();
    });
    connect(leaveOnlineButton, &QPushButton::clicked, this, [this] {
        if (KMessageBox::warningTwoActions(this,
            i18n("Leaving this online match will interrupt it for everyone. Leave Online?"),
            i18nc("@title:window", "Leave Online?"),
            KGuiItem(i18nc("@action:button", "Leave Online")), KStandardGuiItem::cancel())
            == KMessageBox::PrimaryAction)
            Q_EMIT leaveRequested();
    });
    connect(m_hostControls, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_controller) m_controller->setHostControlsEnabled(enabled);
    });
    connect(m_allowVoting, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_controller) m_controller->setAllowVoting(enabled);
    });
    connect(m_proposeButton, &QPushButton::clicked, this, [this] {
        if (m_controller) m_controller->proposeHostAction(m_proposalAction->currentData().toString(), m_proposalTarget->value());
    });
    connect(m_voteYesButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->voteHostProposal(true); });
    connect(m_voteNoButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->voteHostProposal(false); });
    connect(m_rejectButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->decideHostProposal(false); });
    connect(m_withdrawButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->withdrawHostProposal(); });
    connect(m_approveButton, &QPushButton::clicked, this, [this] {
        if (!m_controller) return;
        const auto action = m_proposal.value(QStringLiteral("action")).toString();
        if (m_confirmHostActions->isChecked()
            && (action == QLatin1String("resetHole") || action == QLatin1String("skipHole") || action.startsWith(QLatin1String("go")))) {
            if (KMessageBox::warningTwoActions(this, m_proposal.value(QStringLiteral("summary")).toString(),
                i18nc("@title:window", "Approve Host Proposal?"),
                KGuiItem(i18nc("@action:button", "Approve")), KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction)
                return;
        }
        if (m_controller) m_controller->decideHostProposal(true);
    });
    connect(m_dropButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->chooseDrop(); });
    connect(m_rehitButton, &QPushButton::clicked, this, [this] { if (m_controller) m_controller->chooseRehit(); });
}

SessionController *OnlineMatchView::startMatch(const QJsonObject &config, Net::NetworkClient *network)
{
    finishMatch();
    QStringList playerNames;
    QSet<QString> localPlayerIds;
    for (const auto &value : config.value(QStringLiteral("localPlayerIds")).toArray())
        localPlayerIds.insert(value.toString());
    for (const auto &value : config.value(QStringLiteral("roster")).toArray()) {
        const auto player = value.toObject();
        const auto name = player.value(QStringLiteral("displayName")).toString();
        playerNames.append(localPlayerIds.contains(player.value(QStringLiteral("playerId")).toString())
            ? i18n("%1 (local)", name) : name);
    }
    m_scoreboard->resetPlayers(playerNames);
    m_scoreboard->setOnlineColors(config.value(QStringLiteral("roster")).toArray());

    m_controller = new SessionController(config, network, this, this);
    m_isHost = config.value(QStringLiteral("role")) == QLatin1String("authority");
    m_localMemberId = config.value(QStringLiteral("localMemberId")).toString();
    m_hostOptions->setVisible(m_isHost);
    m_guestStatus->setVisible(!m_isHost);
    m_guestStatus->setText(i18n("Host controls are off."));
    m_proposalComposer->hide();
    m_proposalLabel->hide();
    m_proposalVotes->hide();
    {
        const QSignalBlocker blocker(m_hostControls);
        m_hostControls->setChecked(false);
    }
    {
        const QSignalBlocker blocker(m_allowVoting);
        m_allowVoting->setChecked(true);
    }
    m_hostControls->setEnabled(false);
    m_allowVoting->setEnabled(false);
    m_proposal = {};
    m_proposalLabel->setText(i18n("No active host proposal."));

    connect(m_controller, &SessionController::proposalUiChanged, this, &OnlineMatchView::updateProposalUi);
    connect(m_controller, &SessionController::hostControlsChanged, this,
        [this](bool enabled, bool canToggle, bool, bool, bool) {
            const QSignalBlocker blocker(m_hostControls);
            m_hostControls->setChecked(enabled);
            m_hostControls->setEnabled(canToggle);
        });
    connect(m_controller, &SessionController::hostNavigationChanged, this, [this](int, int highest, bool) {
        m_proposalTarget->setMaximum(qMax(1, highest));
    });
    connect(m_controller, &SessionController::gameReady, this, [this](KolfGame *onlineGame) {
        m_layout->insertWidget(0, onlineGame, 1);
        onlineGame->show();
        onlineGame->setFocus();
    });
    connect(m_controller, &SessionController::noticeChanged, this, &OnlineMatchView::showNotice);
    connect(m_controller, &SessionController::scorecardChanged, m_scoreboard, &ScoreBoard::setOnlineSnapshot);
    connect(m_controller, &SessionController::hazardChoiceChanged, this, &OnlineMatchView::setHazardChoiceAvailable);
    return m_controller;
}

void OnlineMatchView::finishMatch()
{
    {
        const QSignalBlocker blocker(m_hostControls);
        m_hostControls->setChecked(false);
        m_hostControls->setEnabled(false);
    }
    {
        const QSignalBlocker blocker(m_allowVoting);
        m_allowVoting->setChecked(true);
        m_allowVoting->setEnabled(false);
    }
    m_proposal = {};
    m_proposalLabel->setText(i18n("No active host proposal."));
    m_notice->hide();
    setHazardChoiceAvailable(false);
    delete m_controller;
    m_controller = nullptr;
}

bool OnlineMatchView::confirmHostActions() const
{
    return m_confirmHostActions->isChecked();
}

void OnlineMatchView::setConfirmHostActions(bool confirm)
{
    const QSignalBlocker blocker(m_confirmHostActions);
    m_confirmHostActions->setChecked(confirm);
}

void OnlineMatchView::showNotice(const QString &notice)
{
    m_notice->setText(notice);
    m_notice->show();
    QTimer::singleShot(10000, this, [this, notice] {
        if (m_notice->text() == notice) m_notice->hide();
    });
}

void OnlineMatchView::setHazardChoiceAvailable(bool available)
{
    m_hazardPanel->setVisible(available);
    m_dropButton->setEnabled(available);
    m_rehitButton->setEnabled(available);
}

void OnlineMatchView::updateProposalUi(const QJsonObject &state)
{
    const QSignalBlocker blocker(m_allowVoting);
    const bool controlsEnabled = state.value(QStringLiteral("controlsEnabled")).toBool();
    const bool votingEnabled = state.value(QStringLiteral("votingEnabled")).toBool();
    m_allowVoting->setChecked(votingEnabled);
    m_allowVoting->setEnabled(state.value(QStringLiteral("canToggleVoting")).toBool());
    m_guestStatus->setText(!controlsEnabled ? i18n("Host controls are off.")
        : votingEnabled ? i18n("Host controls are on. Voting is open.")
            : i18n("Host controls are on. Voting is off."));
    m_proposal = state.value(QStringLiteral("proposal")).toObject();
    const bool hasProposal = !m_proposal.isEmpty();
    m_proposalComposer->setVisible(!hasProposal && controlsEnabled && votingEnabled);
    m_proposalLabel->setVisible(hasProposal);
    m_proposalVotes->setVisible(hasProposal);
    m_approveButton->setVisible(m_isHost && hasProposal);
    m_rejectButton->setVisible(m_isHost && hasProposal);
    m_withdrawButton->setVisible(hasProposal
        && m_proposal.value(QStringLiteral("proposerId")).toString() == m_localMemberId);
    const bool canPropose = state.value(QStringLiteral("canPropose")).toBool();
    m_proposalAction->setEnabled(canPropose);
    m_proposalTarget->setEnabled(canPropose);
    m_proposeButton->setEnabled(canPropose);
    m_voteYesButton->setEnabled(state.value(QStringLiteral("canVote")).toBool());
    m_voteNoButton->setEnabled(state.value(QStringLiteral("canVote")).toBool());
    m_approveButton->setEnabled(state.value(QStringLiteral("canDecide")).toBool());
    m_rejectButton->setEnabled(state.value(QStringLiteral("canDecide")).toBool());
    m_withdrawButton->setEnabled(state.value(QStringLiteral("canWithdraw")).toBool());
    if (m_proposal.isEmpty()) {
        m_proposalLabel->setText(i18n("No active host proposal."));
        return;
    }
    int yes = 0, no = 0;
    QString ownVote;
    for (const auto &voteValue : m_proposal.value(QStringLiteral("votes")).toArray()) {
        const auto vote = voteValue.toObject();
        if (vote.value(QStringLiteral("approve")).toBool()) ++yes; else ++no;
        if (vote.value(QStringLiteral("memberId")).toString() == m_localMemberId)
            ownVote = vote.value(QStringLiteral("approve")).toBool() ? i18n("Yes") : i18n("No");
    }
    const auto expires = QDateTime::fromMSecsSinceEpoch(qint64(m_proposal.value(QStringLiteral("expiresAt")).toDouble()));
    const auto action = m_proposal.value(QStringLiteral("action")).toString();
    const int actionIndex = m_proposalAction->findData(action);
    QString actionDescription = actionIndex >= 0 ? m_proposalAction->itemText(actionIndex) : action;
    if (action == QLatin1String("goToHole"))
        actionDescription += i18n(" %1", m_proposal.value(QStringLiteral("targetHole")).toInt());
    m_proposalLabel->setText(i18n("%1 proposed %2. %3 Votes: %4 yes, %5 no of %6 members. Expires %7. %8")
        .arg(m_proposal.value(QStringLiteral("proposerName")).toString(),
            actionDescription,
            m_proposal.value(QStringLiteral("summary")).toString())
        .arg(yes).arg(no).arg(m_proposal.value(QStringLiteral("memberCount")).toInt())
        .arg(QLocale().toString(expires, QLocale::ShortFormat),
            ownVote.isEmpty() ? i18n("You have not voted.") : i18n("Your vote: %1.", ownVote)));
}

QJsonObject OnlineMatchView::uiSnapshot() const
{
    return QJsonObject{{QStringLiteral("hostOptionsVisible"), !m_hostOptions->isHidden()},
        {QStringLiteral("guestStatusVisible"), !m_guestStatus->isHidden()},
        {QStringLiteral("composerVisible"), !m_proposalComposer->isHidden()},
        {QStringLiteral("proposalTextVisible"), !m_proposalLabel->isHidden()},
        {QStringLiteral("voteRowVisible"), !m_proposalVotes->isHidden()},
        {QStringLiteral("approveVisible"), !m_approveButton->isHidden()},
        {QStringLiteral("rejectVisible"), !m_rejectButton->isHidden()},
        {QStringLiteral("withdrawVisible"), !m_withdrawButton->isHidden()}};
}
