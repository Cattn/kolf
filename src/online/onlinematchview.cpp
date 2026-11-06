// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinematchview.h"

#include "game.h"
#include "scoreboard.h"
#include "ui/gamehud.h"
#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QMenu>
#include <QToolButton>
#include "session/sessioncontroller.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KStandardGuiItem>

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
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
    auto *hazardTitle = new QLabel(i18n("In a hazard · One penalty stroke. Choose where to play from:"), m_hazardPanel);
    hazardTitle->setWordWrap(true);
    hazardLayout->addWidget(hazardTitle);
    hazardLayout->addStretch();
    m_dropButton = new QPushButton(i18nc("@action:button", "Drop Outside Hazard"), m_hazardPanel);
    m_rehitButton = new QPushButton(i18nc("@action:button", "Rehit"), m_hazardPanel);
    hazardLayout->addWidget(m_dropButton);
    hazardLayout->addWidget(m_rehitButton);
    m_hazardPanel->hide();

    m_matchMenu = new QMenu(this);
    m_matchMenu->setObjectName(QStringLiteral("onlineMatchActions"));
    m_matchButton = new QToolButton(this);
    m_matchButton->setObjectName(QStringLiteral("onlineMatchMenu"));
    m_matchButton->setText(i18n("Match"));
    m_matchButton->setAccessibleName(i18n("Match actions"));
    m_matchButton->setMenu(m_matchMenu);
    m_matchButton->setPopupMode(QToolButton::InstantPopup);
    auto *header = new QHBoxLayout;
    m_hud = new Kolf::Ui::GameHud(this);
    connect(m_hud, &Kolf::Ui::GameHud::hintDismissed, this, [] {
        KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
        settings.writeEntry("firstShotHintShown", true);
        settings.sync();
    });
    header->addWidget(m_hud, 1);
    header->addWidget(m_matchButton, 0, Qt::AlignTop);
    m_layout->insertLayout(0, header);
    m_canvas = new QWidget(this);
    m_canvasLayout = new QGridLayout(m_canvas);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    m_layout->insertWidget(1, m_canvas, 1);
    m_toast = new QFrame(m_canvas);
    m_toast->setObjectName(QStringLiteral("onlineRequestToast"));
    m_toast->setFrameShape(QFrame::StyledPanel);
    m_toast->setAutoFillBackground(true);
    m_toast->setMaximumWidth(520);
    auto *toastLayout = new QVBoxLayout(m_toast);
    m_proposalLabel = new QLabel(m_toast);
    m_proposalLabel->setTextFormat(Qt::PlainText);
    m_proposalLabel->setWordWrap(true);
    m_proposalLabel->setAccessibleName(i18n("Request and vote tally"));
    toastLayout->addWidget(m_proposalLabel);
    m_proposalVotes = new QWidget(m_toast);
    auto *votes = new QHBoxLayout(m_proposalVotes);
    m_voteYesButton = new QPushButton(i18n("Yes"), m_proposalVotes);
    m_voteNoButton = new QPushButton(i18n("No"), m_proposalVotes);
    m_approveButton = new QPushButton(i18n("Approve"), m_proposalVotes);
    m_rejectButton = new QPushButton(i18n("Reject"), m_proposalVotes);
    m_withdrawButton = new QPushButton(i18n("Withdraw"), m_proposalVotes);
    for (auto *button : {m_voteYesButton, m_voteNoButton, m_approveButton, m_rejectButton, m_withdrawButton}) {
        votes->addWidget(button);
    }
    toastLayout->addWidget(m_proposalVotes);
    m_canvasLayout->addWidget(m_notice, 0, 0, Qt::AlignBottom | Qt::AlignLeft);
    m_notice->setAutoFillBackground(true);
    m_notice->setMinimumWidth(260);
    m_notice->setMaximumWidth(420);
    m_canvasLayout->addWidget(m_toast, 0, 0, Qt::AlignTop | Qt::AlignHCenter);
    m_toast->hide();
    m_hazardPanel->setParent(m_canvas);
    m_dropButton->setText(i18n("Drop outside (D)"));
    m_dropButton->setShortcut(QKeySequence(Qt::Key_D));
    m_rehitButton->setText(i18n("Rehit (R)"));
    m_rehitButton->setShortcut(QKeySequence(Qt::Key_R));
    m_hazardPanel->setAutoFillBackground(true);
    m_scoreToggle = new QToolButton(this);
    m_scoreToggle->setObjectName(QStringLiteral("onlineScorecard"));
    m_scoreToggle->setText(i18n("Scorecard"));
    m_scoreToggle->setCheckable(true);
    m_scoreToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_scoreToggle->setArrowType(Qt::RightArrow);
    m_layout->insertWidget(2, m_scoreToggle);
    m_scoreboard->hide();
    connect(m_scoreToggle, &QToolButton::toggled, this, [this](bool open) {
        m_scoreboard->setVisible(open);
        m_scoreToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    auto *rules = m_matchMenu->addMenu(i18n("Rules"));
    m_rulesMenu = rules;
    m_hostControls = rules->addAction(i18n("Host controls"));
    m_hostControls->setCheckable(true);
    m_allowVoting = rules->addAction(i18n("Players can request actions"));
    m_allowVoting->setCheckable(true);
    m_confirmHostActions = rules->addAction(i18n("Confirm host actions"));
    m_confirmHostActions->setCheckable(true);
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    m_confirmHostActions->setChecked(onlineConfig.readEntry("confirmHostActions", true));
    connect(m_confirmHostActions, &QAction::toggled, this, [](bool enabled) {
        KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("Online"));
        config.writeEntry("confirmHostActions", enabled);
        config.sync();
    });
    m_matchMenu->addSeparator();
    const QStringList labels = {i18n("Undo shot"), i18n("Reset hole"), i18n("Skip hole"),
        i18n("Next hole"), i18n("Previous hole"), i18n("First hole"), i18n("Last hole"), i18n("Random hole"), i18n("Choose hole…")};
    const QStringList commands = {QStringLiteral("undoShot"), QStringLiteral("resetHole"), QStringLiteral("skipHole"),
        QStringLiteral("goNext"), QStringLiteral("goPrevious"), QStringLiteral("goFirst"), QStringLiteral("goLast"), QStringLiteral("goRandom"), QStringLiteral("goToHole")};
    auto *go = m_matchMenu->addMenu(i18n("Go to hole"));
    for (int i = 0; i < commands.size(); ++i) {
        auto *action = (i < 3 ? m_matchMenu : go)->addAction(labels[i]);
        action->setData(commands[i]);
        action->setProperty("label", labels[i]);
        m_actions.append(action);
        connect(action, &QAction::triggered, this, [this, action] {
            if (!m_controller) return;
            const QString command = action->data().toString();
            int target = 0;
            if (command == QLatin1String("goToHole")) {
                bool ok = false;
                target = QInputDialog::getInt(this, i18n("Choose hole"), i18n("Hole:"), 1, 1, m_highestHole, 1, &ok);
                if (!ok) return;
            }
            if (!m_isHost) { m_controller->proposeHostAction(command, target); return; }
            if (command != QLatin1String("undoShot") && confirmHostActions()
                && KMessageBox::warningTwoActions(this, i18n("%1?", action->text()), i18n("Change match?"),
                    KGuiItem(i18n("Apply")), KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction) return;
            if (command == QLatin1String("undoShot")) m_controller->undoOnlineShot();
            else if (command == QLatin1String("resetHole")) m_controller->resetOnlineHole();
            else if (command == QLatin1String("skipHole")) m_controller->skipOnlineHole();
            else m_controller->navigateOnline(command, target);
        });
    }
    m_historyMenu = m_matchMenu->addMenu(i18n("Recent notices"));
    m_matchMenu->addSeparator();
    auto *leave = m_matchMenu->addAction(i18n("Leave match…"));
    connect(leave, &QAction::triggered, this, [this] {
        if (KMessageBox::warningTwoActions(this, i18n("Leaving will interrupt this match for everyone. Leave match?"),
            i18n("Leave match?"), KGuiItem(i18n("Leave")), KStandardGuiItem::cancel()) == KMessageBox::PrimaryAction)
            Q_EMIT leaveRequested();
    });
    connect(m_hostControls, &QAction::toggled, this, [this](bool enabled) {
        if (m_controller) m_controller->setHostControlsEnabled(enabled);
    });
    connect(m_allowVoting, &QAction::toggled, this, [this](bool enabled) {
        if (m_controller) m_controller->setAllowVoting(enabled);
    });
    auto *countdown = new QTimer(this);
    countdown->setInterval(1000);
    connect(countdown, &QTimer::timeout, this, &OnlineMatchView::updateCountdown);
    countdown->start();
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
    m_rulesMenu->menuAction()->setVisible(m_isHost);
    m_hostControls->setVisible(m_isHost);
    m_allowVoting->setVisible(m_isHost);
    m_confirmHostActions->setVisible(m_isHost);
    for (auto *action : m_actions) {
        const QString label = action->property("label").toString();
        action->setText(m_isHost ? label : i18n("Request %1", label));
    }
    KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
    if (!settings.readEntry("firstShotHintShown", false))
        m_hud->setHint(settings.readEntry("useAdvancedPutting", false)
            ? i18n("Hold, release at full power, click again to set strength")
            : i18n("Aim with the mouse · hold to charge · release to putt"));
    m_scoreToggle->setChecked(false);
    m_historyMenu->clear();
    m_toast->hide();
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
        [this](bool enabled, bool canToggle, bool canReset, bool canUndo, bool canSkip) {
            const QSignalBlocker blocker(m_hostControls);
            m_hostControls->setChecked(enabled);
            m_hostControls->setEnabled(canToggle);
            if (m_isHost) {
                m_actions[0]->setEnabled(canUndo);
                m_actions[1]->setEnabled(canReset);
                m_actions[2]->setEnabled(canSkip);
            }
        });
    connect(m_controller, &SessionController::hostNavigationChanged, this, [this](int, int highest, bool canNavigate) {
        m_highestHole = qMax(1, highest);
        if (m_isHost) for (int i = 3; i < m_actions.size(); ++i) m_actions[i]->setEnabled(canNavigate);
    });
    connect(m_controller, &SessionController::gameReady, this, [this](KolfGame *onlineGame) {
        m_game = onlineGame;
        m_canvasLayout->addWidget(onlineGame, 0, 0);
        onlineGame->lower();
        m_notice->raise();
        m_toast->raise();
        m_hazardPanel->raise();
        onlineGame->show();
        onlineGame->setFocus();
    });
    connect(m_controller, &SessionController::noticeChanged, this, &OnlineMatchView::showNotice);
    connect(m_controller, &SessionController::scorecardChanged, m_scoreboard, &ScoreBoard::setOnlineSnapshot);
    connect(m_controller, &SessionController::statusChanged, m_hud, &Kolf::Ui::GameHud::setMessage);
    connect(m_controller, &SessionController::scorecardChanged, this, [this](const QJsonArray &, const QJsonArray &, int slot, int hole) {
        if (!m_game) return;
        m_hud->setHole(hole, m_game->holeCount(), m_game->holePar());
        if (m_controller->phase() == QLatin1String("AwaitingShot"))
            m_hud->setTurn(m_game->currentPlayerName(), m_game->currentPlayerColor(), m_game->currentPlayerStrokes() + 1, m_controller->ownsSlot(slot), true);
        if (hole != m_lastHole) { m_scoreToggle->setChecked(true); m_lastHole = hole; }
    });
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
    m_game = nullptr;
    m_lastHole = 1;
    m_toast->hide();
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
    if (m_notice->text() != notice) {
        m_historyMenu->addAction(notice)->setEnabled(false);
        if (m_historyMenu->actions().size() > 5) delete m_historyMenu->actions().first();
    }
    m_notice->setText(notice);
    m_notice->show();
    QTimer::singleShot(10000, this, [this, notice] {
        if (m_notice->text() == notice) m_notice->hide();
    });
}

void OnlineMatchView::setHazardChoiceAvailable(bool available)
{
    if (available && m_game && m_game->hasCurrentPlayer()) {
        m_hazardPanel->setMaximumWidth(qMin(620, m_canvas->width()));
        m_hazardPanel->adjustSize();
        const QPoint ball = m_game->mapTo(m_canvas, m_game->mapFromScene(m_game->curBall()->scenePos()));
        m_hazardPanel->move(qBound(0, ball.x() + 20, qMax(0, m_canvas->width() - m_hazardPanel->width())),
            qBound(0, ball.y() + 20, qMax(0, m_canvas->height() - m_hazardPanel->height())));
        m_hazardPanel->raise();
    }
    m_hazardPanel->setVisible(available);
    m_dropButton->setEnabled(available);
    m_rehitButton->setEnabled(available);
}

void OnlineMatchView::updateProposalUi(const QJsonObject &state)
{
    const QSignalBlocker blocker(m_allowVoting);
    const bool votingEnabled = state.value(QStringLiteral("votingEnabled")).toBool();
    m_allowVoting->setChecked(votingEnabled);
    m_allowVoting->setEnabled(state.value(QStringLiteral("canToggleVoting")).toBool());
    m_proposal = state.value(QStringLiteral("proposal")).toObject();
    const bool hasProposal = !m_proposal.isEmpty();
    m_toast->setVisible(hasProposal);
    m_proposalLabel->setVisible(hasProposal);
    m_proposalVotes->setVisible(hasProposal);
    m_approveButton->setVisible(m_isHost && hasProposal);
    m_rejectButton->setVisible(m_isHost && hasProposal);
    m_withdrawButton->setVisible(hasProposal
        && m_proposal.value(QStringLiteral("proposerId")).toString() == m_localMemberId);
    const bool canPropose = state.value(QStringLiteral("canPropose")).toBool();
    if (!m_isHost) for (auto *action : m_actions) action->setEnabled(canPropose);
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
    m_voteSummary = i18n("%1 asks to %2 · Yes %3 · No %4")
        .arg(m_proposal.value(QStringLiteral("proposerName")).toString(), m_proposal.value(QStringLiteral("summary")).toString())
        .arg(yes).arg(no);
    if (!ownVote.isEmpty()) m_voteSummary += i18n(" · Your vote: %1", ownVote);
    updateCountdown();
}

void OnlineMatchView::updateCountdown()
{
    if (m_proposal.isEmpty()) return;
    const qint64 seconds = qMax(qint64(0), (qint64(m_proposal.value(QStringLiteral("expiresAt")).toDouble()) - QDateTime::currentMSecsSinceEpoch()) / 1000);
    m_proposalLabel->setText(i18n("%1 · %2 s remaining", m_voteSummary, seconds));
}

QJsonObject OnlineMatchView::uiSnapshot() const
{
    return QJsonObject{{QStringLiteral("hudVisible"), m_hud->isVisible()},
        {QStringLiteral("matchMenuVisible"), m_matchButton->isVisible()},
        {QStringLiteral("rulesVisible"), m_hostControls->isVisible()},
        {QStringLiteral("requestsEnabled"), !m_isHost && m_actions[0]->isEnabled()},
        {QStringLiteral("toastVisible"), !m_toast->isHidden()},
        {QStringLiteral("proposalTextVisible"), !m_proposalLabel->isHidden()},
        {QStringLiteral("voteRowVisible"), !m_proposalVotes->isHidden()},
        {QStringLiteral("approveVisible"), !m_approveButton->isHidden()},
        {QStringLiteral("rejectVisible"), !m_rejectButton->isHidden()},
        {QStringLiteral("withdrawVisible"), !m_withdrawButton->isHidden()}};
}
