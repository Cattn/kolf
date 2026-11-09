/*
    Copyright (C) 2002-2005, Jason Katz-Brown <jasonkb@mit.edu>
    Copyright 2010 Stefan Majewsky <majewsky@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "effects.h"
#include "kolf.h"
#include "editor.h"
#include "landscape.h"
#include "newgame.h"
#include "objects.h"
#include "obstacles.h"
#include "online/onlinematchview.h"
#include "ui/gamehud.h"
#include "ui/homeview.h"
#include "ui/newgameview.h"
#include "ui/resultsview.h"
#include "results/summary.h"
#include "ui/settingsdialog.h"
#include "online/onlinewidget.h"
#include "online/invitelink.h"
#include "scoreboard.h"
#include "session/sessioncontroller.h"
#ifdef KOLF_TEST_AUTOMATION
#include "testing/onlineautomation.h"
#endif

#include <KGameHighScoreDialog>
#include <KGameStandardAction>

#include <KActionCollection>
#include <KIO/FileCopyJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSelectAction>
#include <KSharedConfig>
#include <KStandardAction>
#include <KStandardGuiItem>
#include <KToggleAction>

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMimeDatabase>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>

static QAction *goMenuAction(QMenuBar *bar)
{
	auto *menu = bar->findChild<QMenu *>(QStringLiteral("go_course"));
	return menu ? menu->menuAction() : nullptr;
}

KolfWindow::KolfWindow()
    : KXmlGuiWindow(nullptr)
{
	setObjectName( QStringLiteral("Kolf" ));
	competition = false;
	game = nullptr;
	editor = nullptr;
	spacer = nullptr;
	scoreboard = nullptr;
	isTutorial = false;

	setupActions();

	//NOTE: The plugin mechanism has been removed because it is not used anyway.

	filename = QString();
	applicationStack = new QStackedWidget(this);
	dummy = new QWidget(applicationStack);
	applicationStack->addWidget(dummy);
	setCentralWidget(applicationStack);
	layout = new QGridLayout(dummy);
	gameHud = new Kolf::Ui::GameHud(dummy);
	layout->addWidget(gameHud, 0, 0);
	gameHud->hide();
	connect(gameHud, &Kolf::Ui::GameHud::hintDismissed, this, [] {
		KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
		settings.writeEntry("firstShotHintShown", true);
		settings.sync();
	});
	homeView = new Kolf::Ui::HomeView(dummy);
	homeView->hide();
	connect(homeView, &Kolf::Ui::HomeView::playRequested, this, &KolfWindow::newGame);
	connect(homeView, &Kolf::Ui::HomeView::onlineRequested, this, &KolfWindow::showOnline);
	connect(homeView, &Kolf::Ui::HomeView::editorRequested, this, &KolfWindow::editNewCourse);
	connect(homeView, &Kolf::Ui::HomeView::tutorialRequested, this, &KolfWindow::tutorial);
	connect(homeView, &Kolf::Ui::HomeView::continueRequested, this, &KolfWindow::continueGame);

	setCourseEditingActionsVisible(false);
	setMinimumSize(640, 560);
	resize(960, 760);
}

KolfWindow::~KolfWindow()
{
}

void KolfWindow::setupActions()
{
	// Game
	newAction = KGameStandardAction::gameNew(this, &KolfWindow::newGame, actionCollection());
	onlineAction = actionCollection()->addAction(QStringLiteral("online"));
	onlineAction->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
	onlineAction->setText(i18nc("@action", "Play &Online…"));
	connect(onlineAction, &QAction::triggered, this, &KolfWindow::showOnline);
	onlineResyncAction = actionCollection()->addAction(QStringLiteral("online_resync"));
	onlineResyncAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
	onlineResyncAction->setText(i18nc("@action", "Resynchronize Online Match"));
	onlineResyncAction->setVisible(false);
	connect(onlineResyncAction, &QAction::triggered, this, [this] {
		if (onlineMatchController) onlineMatchController->requestResync();
	});
	endAction = KGameStandardAction::end(this, &KolfWindow::closeGame, actionCollection());
	homeAction = actionCollection()->addAction(QStringLiteral("mainmenu"));
	homeAction->setIcon(QIcon::fromTheme(QStringLiteral("go-home")));
	homeAction->setText(i18nc("@action", "&Main Menu"));
	connect(homeAction, &QAction::triggered, this, &KolfWindow::returnHome);
	KGameStandardAction::quit(this, &KolfWindow::close, actionCollection());

	saveAction = actionCollection()->addAction(KStandardAction::Save, QStringLiteral("course_save"), this, &KolfWindow::save);
	saveAction->setText(i18nc("@action", "Save &Course"));
	saveAsAction = actionCollection()->addAction(KStandardAction::SaveAs, QStringLiteral("course_save_as"), this, &KolfWindow::saveAs);
	saveAsAction->setText(i18nc("@action", "Save &Course As…"));

	saveGameAction = actionCollection()->addAction(QStringLiteral("savegame"));
	saveGameAction->setText(i18nc("@action", "&Save Game"));
	connect(saveGameAction, &QAction::triggered, this, &KolfWindow::saveGame);
	saveGameAsAction = actionCollection()->addAction(QStringLiteral("savegameas"));
	saveGameAsAction->setText(i18nc("@action", "&Save Game As…"));
	connect(saveGameAsAction, &QAction::triggered, this, &KolfWindow::saveGameAs);

	loadGameAction = KGameStandardAction::load(this, &KolfWindow::loadGame, actionCollection());
	highScoreAction = KGameStandardAction::highscores(this, &KolfWindow::showHighScores, actionCollection());

	// Hole
	editingAction = new KToggleAction(QIcon::fromTheme( QStringLiteral( "document-properties") ), i18nc("@option:check", "&Edit Course"), this);
	actionCollection()->addAction(QStringLiteral("editing"), editingAction);
	connect(editingAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(editingAction, Qt::CTRL|Qt::Key_E);
	newHoleAction = actionCollection()->addAction(QStringLiteral("newhole"));
	newHoleAction->setIcon(QIcon::fromTheme( QStringLiteral( "document-new" )));
	newHoleAction->setText(i18nc("@action", "&New Hole"));
	connect(newHoleAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(newHoleAction, Qt::CTRL|Qt::SHIFT|Qt::Key_N);
	clearHoleAction = actionCollection()->addAction(QStringLiteral("clearhole"));
	clearHoleAction->setIcon(QIcon::fromTheme( QStringLiteral( "edit-clear-locationbar-ltr" )));
	clearHoleAction->setText(i18nc("@action", "&Clear Hole"));
	connect(clearHoleAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(clearHoleAction, Qt::CTRL|Qt::Key_Delete);
	resetHoleAction = actionCollection()->addAction(QStringLiteral("resethole"));
	resetHoleAction->setText(i18nc("@action", "&Reset Hole"));
	connect(resetHoleAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(resetHoleAction, Qt::CTRL|Qt::Key_R);
	undoShotAction = KStandardAction::undo(this, &KolfWindow::emptySlot, this);
	actionCollection()->addAction(QStringLiteral("undoshot"), undoShotAction);
	undoShotAction->setText(i18nc("@action", "&Undo Shot"));
	skipHoleAction = actionCollection()->addAction(QStringLiteral("skiponlinehole"));
	skipHoleAction->setText(i18nc("@action", "&Skip Hole"));
	skipHoleAction->setVisible(false);
	skipHoleAction->setEnabled(false);
	//replayShotAction = new QAction(i18nc("@action", "&Replay Shot"), 0, this, SLOT(emptySlot()), actionCollection(), "replay");

	// Go
	holeAction = new KSelectAction(i18nc("@title:menu", "&Hole Number"), this);
	actionCollection()->addAction(QStringLiteral("switchhole"), holeAction);
	connect(holeAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	nextAction = actionCollection()->addAction(QStringLiteral("nexthole"));
	nextAction->setIcon(QIcon::fromTheme( QStringLiteral( "go-next" )));
	nextAction->setText(i18nc("@action", "&Next Hole"));
	connect(nextAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcuts(nextAction, KStandardShortcut::forward());
	prevAction = actionCollection()->addAction(QStringLiteral("prevhole"));
	prevAction->setIcon(QIcon::fromTheme( QStringLiteral( "go-previous" )));
	prevAction->setText(i18nc("@action", "&Previous Hole"));
	connect(prevAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcuts(prevAction, KStandardShortcut::back());
	firstAction = actionCollection()->addAction(QStringLiteral("firsthole"));
	firstAction->setIcon(QIcon::fromTheme( QStringLiteral( "go-home" )));
	firstAction->setText(i18nc("@action", "&First Hole"));
	connect(firstAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcuts(firstAction, KStandardShortcut::begin());
	lastAction = actionCollection()->addAction(QStringLiteral("lasthole"));
	lastAction->setText(i18nc("@action", "&Last Hole"));
	connect(lastAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(lastAction, Qt::CTRL|Qt::SHIFT|Qt::Key_End); // why not KStandardShortcut::End (Ctrl+End)?
	randAction = actionCollection()->addAction(QStringLiteral("randhole"));
	randAction->setIcon(QIcon::fromTheme( QStringLiteral( "go-jump" )));
	randAction->setText(i18nc("@action", "&Random Hole"));
	connect(randAction, &QAction::triggered, this, &KolfWindow::emptySlot);

	// Settings
	useMouseAction = new KToggleAction(i18nc("@option:check", "Enable &Mouse for Moving Putter"), this);
	actionCollection()->addAction(QStringLiteral("usemouse"), useMouseAction);
	connect(useMouseAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	connect(useMouseAction, &QAction::toggled, this, &KolfWindow::useMouseChanged);
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings"));
	useMouseAction->setChecked(configGroup.readEntry("useMouse", true));

	useAdvancedPuttingAction = new KToggleAction(i18nc("@option:check", "Enable &Advanced Putting"), this);
	actionCollection()->addAction(QStringLiteral("useadvancedputting"), useAdvancedPuttingAction);
	connect(useAdvancedPuttingAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	connect(useAdvancedPuttingAction, &QAction::toggled, this, &KolfWindow::useAdvancedPuttingChanged);
	useAdvancedPuttingAction->setChecked(configGroup.readEntry("useAdvancedPutting", false));

	showInfoAction = new KToggleAction(QIcon::fromTheme( QStringLiteral( "help-about")), i18nc("@option:check", "Show &Info"), this);
	actionCollection()->addAction(QStringLiteral("showinfo"), showInfoAction);
	connect(showInfoAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	KActionCollection::setDefaultShortcut(showInfoAction, Qt::CTRL|Qt::Key_I);
	connect(showInfoAction, &QAction::toggled, this, &KolfWindow::showInfoChanged);
	showInfoAction->setChecked(configGroup.readEntry("showInfo", true));

	showGuideLineAction = new KToggleAction(i18nc("@option:check", "Show Putter &Guideline"), this);
	actionCollection()->addAction(QStringLiteral("showguideline"), showGuideLineAction);
	connect(showGuideLineAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	connect(showGuideLineAction, &QAction::toggled, this, &KolfWindow::showGuideLineChanged);
	showGuideLineAction->setChecked(configGroup.readEntry("showGuideLine", true));

	effectsAction = new KSelectAction(i18nc("@title:menu", "&Effects"), this);
	actionCollection()->addAction(QStringLiteral("effects"), effectsAction);
	effectsAction->setItems({i18nc("@item:inmenu effects level", "Full"), i18nc("@item:inmenu effects level", "Reduced"), i18nc("@item:inmenu effects level", "Off")});
	effectsAction->setCurrentItem(int(Kolf::Effects::level()));
	connect(effectsAction, &KSelectAction::indexTriggered, this, [](int index) { Kolf::Effects::setLevel(Kolf::Effects::Level(index)); });

	ballTrailsAction = new KToggleAction(i18nc("@option:check", "Show Ball &Trails"), this);
	actionCollection()->addAction(QStringLiteral("balltrails"), ballTrailsAction);
	ballTrailsAction->setChecked(Kolf::Effects::ballTrails());
	connect(ballTrailsAction, &QAction::toggled, this, [](bool enabled) { Kolf::Effects::setBallTrails(enabled); });

	plainModeAction = new KToggleAction(i18nc("@option:check", "&Plain Mode"), this);
	plainModeAction->setToolTip(i18nc("@info:tooltip", "Flat, high-contrast colours with no decorations"));
	actionCollection()->addAction(QStringLiteral("plainmode"), plainModeAction);
	plainModeAction->setChecked(Kolf::Effects::plainMode());
	connect(plainModeAction, &QAction::toggled, this, [](bool plain) { Kolf::Effects::setPlainMode(plain); });

	KStandardAction::preferences(this, &KolfWindow::showSettings, actionCollection());

	soundAction = new KToggleAction(QIcon::fromTheme(QStringLiteral("speaker")),
	                                i18nc("@option:check", "Play Sounds"), this);
	actionCollection()->addAction(QStringLiteral("sound"), soundAction);
	connect(soundAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	connect(soundAction, &QAction::toggled, this, &KolfWindow::soundChanged);
	soundAction->setChecked(configGroup.readEntry("sound", true));

	aboutAction = actionCollection()->addAction(QStringLiteral("aboutcourse"));
	aboutAction->setText(i18nc("@action", "&About Course"));
	connect(aboutAction, &QAction::triggered, this, &KolfWindow::emptySlot);
	tutorialAction = actionCollection()->addAction(QStringLiteral("tutorial"));
	tutorialAction->setText(i18nc("@action", "&How to Play"));
	connect(tutorialAction, &QAction::triggered, this, &KolfWindow::tutorial);

	setupGUI();
}

void KolfWindow::ensureOnlineWidget()
{
	if (!onlineWidget) {
		onlineWidget = new Kolf::Online::OnlineWidget(applicationStack);
		applicationStack->addWidget(onlineWidget);
		connect(onlineWidget, &Kolf::Online::OnlineWidget::leaveRequested, this, &KolfWindow::leaveOnline);
		connect(onlineWidget, &Kolf::Online::OnlineWidget::manageServersRequested, this, &KolfWindow::showServerSettings, Qt::QueuedConnection);
		connect(onlineWidget, &Kolf::Online::OnlineWidget::matchRequested, this, &KolfWindow::startOnlineMatch);
		connect(onlineWidget, &Kolf::Online::OnlineWidget::matchEnded, this, &KolfWindow::finishOnlineMatch);
		connect(onlineWidget, &Kolf::Online::OnlineWidget::matchPauseChanged, this, [this](bool paused, const QString &reason) {
			if (!onlineMatchController) return;
			onlineMatchController->setPaused(paused, reason);
			if (paused && onlineMatchView) onlineMatchView->showNotice(reason);
		});
		connect(onlineWidget, &Kolf::Online::OnlineWidget::matchTransportResumed, this, [this] {
			if (onlineMatchController) onlineMatchController->resumeAfterReconnect();
		});
		connect(onlineWidget, &Kolf::Online::OnlineWidget::serviceNotice, this, [this](const QString &text) {
			statusBar()->showMessage(text);
			if (onlineMatchController && onlineMatchView)
				onlineMatchView->showNotice(text);
		});
		connect(onlineWidget, &Kolf::Online::OnlineWidget::statusChanged, this, [this](const QString &status) {
			if (!onlineMatchController)
				statusBar()->showMessage(status);
		});
	}
}

void KolfWindow::openInviteUrl(const QString &link)
{
	const auto invite = Kolf::Online::parseInviteLink(link);
	if (!invite) {
		KMessageBox::error(this, i18n("That invite link isn't valid. Ask for the join code instead."));
		return;
	}
	raise();
	activateWindow();
	if (onlineMatchController) {
		if (KMessageBox::warningTwoActions(this, i18n("You're in an online match. Leave it to join the new game?"),
				i18nc("@title:window", "Leave Online Match?"),
				KGuiItem(i18nc("@action:button", "Leave and Join"), QStringLiteral("network-disconnect")),
				KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction)
			return;
		leaveOnline();
	}
	showOnline();
	onlineWidget->openInvite(invite->code, invite->serverUrl);
}

void KolfWindow::showOnline()
{
	if (onlineModeActive)
		return;
	ensureOnlineWidget();
	onlineModeActive = true;
	offlineHoleItems = holeAction->items();
	offlineHoleIndex = holeAction->currentItem();
	if (game) {
		for (auto *action : {nextAction, prevAction, firstAction, lastAction, randAction})
			disconnect(action, nullptr, game, nullptr);
		disconnect(holeAction, nullptr, game, nullptr);
	}
	if (game)
		disconnect(resetHoleAction, &QAction::triggered, game, &KolfGame::resetHole);
	if (game)
		disconnect(undoShotAction, &QAction::triggered, game, &KolfGame::undoShot);
	offlineActionStates.clear();
	const QList<QAction *> offlineActions{
		editingAction, newHoleAction, resetHoleAction, undoShotAction, clearHoleAction,
		tutorialAction, newAction, endAction, saveAction, saveAsAction, saveGameAction,
		saveGameAsAction, loadGameAction, aboutAction, holeAction, highScoreAction,
		nextAction, prevAction, firstAction, lastAction, randAction,
	};
	for (auto *action : offlineActions) {
		offlineActionStates.insert(action, action->isEnabled());
		action->setEnabled(false);
	}
	for (QAction *action : QList<QAction *>{resetHoleAction, undoShotAction, holeAction,
		nextAction, prevAction, firstAction, lastAction, randAction}) action->setVisible(false);
	if (auto *goMenu = goMenuAction(menuBar())) goMenu->setVisible(false);
	onlineAction->setEnabled(false);
	offlineGamePausedForOnline = game != nullptr;
	if (offlineGamePausedForOnline)
		game->pause();
	applicationStack->setCurrentWidget(onlineWidget);
	onlineWidget->setFocus();
#ifdef KOLF_TEST_AUTOMATION
	if (onlineAutomation)
		return;
#endif
	onlineWidget->enterOnline();
}

void KolfWindow::leaveOnline()
{
	if (!onlineModeActive || !onlineWidget)
		return;
	onlineWidget->leaveOnline();
	finishOnlineMatch();
	applicationStack->setCurrentWidget(dummy);
	{
		const QSignalBlocker blocker(holeAction);
		holeAction->setItems(offlineHoleItems);
		if (offlineHoleIndex >= 0 && offlineHoleIndex < offlineHoleItems.size())
			holeAction->setCurrentItem(offlineHoleIndex);
	}
	if (game) {
		connect(holeAction, &KSelectAction::indexTriggered, game,
			[offlineGame = game](int index) { offlineGame->switchHole(index + 1); });
		connect(nextAction, &QAction::triggered, game, &KolfGame::nextHole);
		connect(prevAction, &QAction::triggered, game, &KolfGame::prevHole);
		connect(firstAction, &QAction::triggered, game, &KolfGame::firstHole);
		connect(lastAction, &QAction::triggered, game, &KolfGame::lastHole);
		connect(randAction, &QAction::triggered, game, &KolfGame::randHole);
	}
	for (auto it = offlineActionStates.constBegin(); it != offlineActionStates.constEnd(); ++it)
		it.key()->setEnabled(it.value());
	for (QAction *action : QList<QAction *>{resetHoleAction, undoShotAction, holeAction,
		nextAction, prevAction, firstAction, lastAction, randAction}) action->setVisible(true);
	if (auto *goMenu = goMenuAction(menuBar())) goMenu->setVisible(true);
	if (game)
		connect(resetHoleAction, &QAction::triggered, game, &KolfGame::resetHole, Qt::UniqueConnection);
	if (game)
		connect(undoShotAction, &QAction::triggered, game, &KolfGame::undoShot, Qt::UniqueConnection);
	offlineActionStates.clear();
	offlineHoleItems.clear();
	offlineHoleIndex = -1;
	onlineAction->setEnabled(true);
	if (offlineGamePausedForOnline && game) {
		game->unPause();
		game->setFocus();
	}
	offlineGamePausedForOnline = false;
	onlineModeActive = false;
	statusBar()->clearMessage();
}

void KolfWindow::startOnlineMatch(const QJsonObject &matchConfig)
{
#ifdef KOLF_TEST_AUTOMATION
	const QJsonObject config = onlineAutomation ? onlineAutomation->sessionConfig(matchConfig) : matchConfig;
#else
	const QJsonObject &config = matchConfig;
#endif
	finishOnlineMatch();
	if (!onlineMatchView) {
		onlineMatchView = new Kolf::Online::OnlineMatchView(applicationStack);
		applicationStack->addWidget(onlineMatchView);
		connect(onlineMatchView, &Kolf::Online::OnlineMatchView::changeServerRequested, this, [this] {
			onlineWidget->changeServer();
		});
		connect(onlineMatchView, &Kolf::Online::OnlineMatchView::leaveRequested, this, &KolfWindow::leaveOnline);
	}

	onlineMatchController = onlineMatchView->startMatch(config, onlineWidget->networkClient());
	const bool isHost = config.value(QStringLiteral("role")) == QLatin1String("authority");
	resetHoleAction->setEnabled(false);
	undoShotAction->setEnabled(false);
	for (QAction *action : QList<QAction *>{resetHoleAction, undoShotAction, holeAction,
		nextAction, prevAction, firstAction, lastAction, randAction}) action->setVisible(isHost);
	if (auto *goMenu = goMenuAction(menuBar())) goMenu->setVisible(isHost);
	skipHoleAction->setVisible(isHost);
	skipHoleAction->setEnabled(false);
	connect(onlineMatchController, &Kolf::Session::SessionController::hostControlsChanged, this,
		[this](bool, bool, bool canReset, bool canUndo, bool canSkip) {
			resetHoleAction->setEnabled(canReset);
			undoShotAction->setEnabled(canUndo);
			skipHoleAction->setEnabled(canSkip);
		});
	connect(undoShotAction, &QAction::triggered, onlineMatchController,
		&Kolf::Session::SessionController::undoOnlineShot);
	connect(resetHoleAction, &QAction::triggered, onlineMatchController, [this] {
		if (!onlineMatchView->confirmHostActions() || KMessageBox::warningTwoActions(this,
			i18n("Reset this hole? Every player's strokes and score on this hole will be cleared."),
			i18nc("@title:window", "Reset Online Hole?"),
			KGuiItem(i18nc("@action:button", "Reset Hole")), KStandardGuiItem::cancel())
			== KMessageBox::PrimaryAction)
			onlineMatchController->resetOnlineHole();
	});
	connect(skipHoleAction, &QAction::triggered, onlineMatchController, [this] {
		if (!onlineMatchView->confirmHostActions() || KMessageBox::warningTwoActions(this,
			i18n("Skip this hole? Current strokes will stay on the scorecard, but the hole will count as unfinished. Players who have not taken a stroke remain unscored."),
			i18nc("@title:window", "Skip Online Hole?"),
			KGuiItem(i18nc("@action:button", "Skip Hole")), KStandardGuiItem::cancel())
			== KMessageBox::PrimaryAction)
			onlineMatchController->skipOnlineHole();
	});
	connect(onlineMatchController, &Kolf::Session::SessionController::hostNavigationChanged, this,
		[this](int current, int highest, bool available) {
			onlineCurrentHole = current;
			QStringList items;
			for (int hole = 1; hole <= highest; ++hole) items.append(QString::number(hole));
			{
				const QSignalBlocker blocker(holeAction);
				holeAction->setItems(items);
				if (current >= 1 && current <= highest) holeAction->setCurrentItem(current - 1);
			}
			holeAction->setEnabled(available && highest > 1);
			nextAction->setEnabled(available && current < highest);
			prevAction->setEnabled(available && current > 1);
			firstAction->setEnabled(available && current > 1);
			lastAction->setEnabled(available && current < highest);
			randAction->setEnabled(available && highest > 1);
		});
	const auto confirmGo = [this](const QString &action, int targetHole) {
		const QString destination = action == QLatin1String("goRandom") ? i18n("a random hole")
			: i18n("Hole %1", targetHole);
		if (!onlineMatchView->confirmHostActions() || KMessageBox::warningTwoActions(this,
			i18n("Move to %1? The current hole will be left unfinished. The destination will restart and its previous scores and shot counts will be cleared.", destination),
			i18nc("@title:window", "Move to Online Hole?"),
			KGuiItem(i18nc("@action:button", "Move to Hole")), KStandardGuiItem::cancel())
			== KMessageBox::PrimaryAction)
			onlineMatchController->navigateOnline(action, targetHole);
		else {
			const QSignalBlocker blocker(holeAction);
			holeAction->setCurrentItem(onlineCurrentHole - 1);
		}
	};
	connect(nextAction, &QAction::triggered, onlineMatchController, [this, confirmGo] {
		confirmGo(QStringLiteral("goNext"), onlineCurrentHole + 1);
	});
	connect(prevAction, &QAction::triggered, onlineMatchController, [this, confirmGo] {
		confirmGo(QStringLiteral("goPrevious"), onlineCurrentHole - 1);
	});
	connect(firstAction, &QAction::triggered, onlineMatchController, [confirmGo] { confirmGo(QStringLiteral("goFirst"), 1); });
	connect(lastAction, &QAction::triggered, onlineMatchController, [this, confirmGo] {
		confirmGo(QStringLiteral("goLast"), holeAction->items().size());
	});
	connect(randAction, &QAction::triggered, onlineMatchController, [confirmGo] { confirmGo(QStringLiteral("goRandom"), 0); });
	connect(holeAction, &KSelectAction::indexTriggered, onlineMatchController, [confirmGo](int index) {
		confirmGo(QStringLiteral("goToHole"), index + 1);
	});
	connect(onlineMatchController, &Kolf::Session::SessionController::statusChanged, this, [this](const QString &status) {
		statusBar()->showMessage(status);
	});
	connect(onlineMatchController, &Kolf::Session::SessionController::noticeChanged, this, [this](const QString &notice) {
		statusBar()->showMessage(notice, 10000);
	});
	connect(useMouseAction, &QAction::toggled, onlineMatchController, &Kolf::Session::SessionController::setUseMouse);
	connect(useAdvancedPuttingAction, &QAction::toggled, onlineMatchController, &Kolf::Session::SessionController::setUseAdvancedPutting);
	connect(soundAction, &QAction::toggled, onlineMatchController, &Kolf::Session::SessionController::setSound);
	connect(showInfoAction, &QAction::toggled, onlineMatchController, &Kolf::Session::SessionController::setShowInfo);
	connect(showGuideLineAction, &QAction::toggled, onlineMatchController, &Kolf::Session::SessionController::setShowGuideLine);
	onlineMatchController->setUseMouse(useMouseAction->isChecked());
	onlineMatchController->setUseAdvancedPutting(useAdvancedPuttingAction->isChecked());
	onlineMatchController->setSound(soundAction->isChecked());
	onlineMatchController->setShowInfo(showInfoAction->isChecked());
	onlineMatchController->setShowGuideLine(showGuideLineAction->isChecked());
	onlineResyncAction->setVisible(true);
	onlineResyncAction->setEnabled(true);
	applicationStack->setCurrentWidget(onlineMatchView);
	statusBar()->showMessage(i18n("Preparing the online match…"));
#ifdef KOLF_TEST_AUTOMATION
	if (onlineAutomation)
		onlineAutomation->attach(onlineMatchController, config);
#endif
}

void KolfWindow::finishOnlineMatch()
{
	onlineResyncAction->setVisible(false);
	if (onlineModeActive) { resetHoleAction->setEnabled(false); undoShotAction->setEnabled(false); }
	if (onlineModeActive) for (QAction *action : QList<QAction *>{holeAction, nextAction, prevAction, firstAction, lastAction, randAction})
		action->setEnabled(false);
	if (onlineModeActive) for (QAction *action : QList<QAction *>{resetHoleAction, undoShotAction, holeAction,
		nextAction, prevAction, firstAction, lastAction, randAction}) action->setVisible(false);
	if (onlineModeActive) {
		if (auto *goMenu = goMenuAction(menuBar())) goMenu->setVisible(false);
	}
	skipHoleAction->setEnabled(false);
	skipHoleAction->setVisible(false);
	if (onlineMatchView)
		onlineMatchView->finishMatch();
	onlineMatchController = nullptr;
	if (onlineModeActive && onlineWidget)
		applicationStack->setCurrentWidget(onlineWidget);
}

#ifdef KOLF_TEST_AUTOMATION
void KolfWindow::startOnlineAutomation(const QJsonObject &config)
{
	ensureOnlineWidget();
	onlineAutomation = new Kolf::Testing::OnlineAutomation(config, onlineWidget,
		[this] { return onlineUiSnapshot(); }, this);
	showOnline();
	onlineAutomation->start();
}

QJsonObject KolfWindow::onlineUiSnapshot() const
{
	auto snapshot = onlineMatchView ? onlineMatchView->uiSnapshot() : QJsonObject();
	const auto *goMenu = goMenuAction(menuBar());
	snapshot[QStringLiteral("resetMenuVisible")] = resetHoleAction->isVisible();
	snapshot[QStringLiteral("goMenuActionVisible")] = nextAction->isVisible();
	snapshot[QStringLiteral("goMenuVisible")] = goMenu && goMenu->isVisible();
	return snapshot;
}
#endif

bool KolfWindow::queryClose()
{
	if (onlineMatchController) {
		const auto result = KMessageBox::warningTwoActions(this,
			i18n("An online match is still in progress. Leaving now will end the match for everyone."),
			i18nc("@title:window", "Leave Online Match?"),
			KGuiItem(i18nc("@action:button", "Leave Match"), QStringLiteral("network-disconnect")),
			KStandardGuiItem::cancel());
		if (result != KMessageBox::PrimaryAction)
			return false;
	}
	if (game)
		if (game->askSave(true))
			return false;
	if (onlineModeActive)
		leaveOnline();
	return true;
}

void KolfWindow::startNewGame()
{
	int firstHole = 1;

	if (loadedGame.isNull())
	{
		showSetup(filename);
		return;
	}

	players.clear();
	{
		KConfig config(loadedGame);
		KConfigGroup configGroup(config.group(QStringLiteral("0 Saved Game")));

		if (isTutorial)
			filename = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("tutorial.kolf"));
		else
			filename = configGroup.readEntry("Course", QString());

		if (filename.isNull())
			return;

		competition = configGroup.readEntry("Competition", false);
		m_savedRandomSeed = configGroup.hasKey("Random Seed") ? qint64(configGroup.readEntry("Random Seed", 0u)) : -1;
		firstHole = configGroup.readEntry("Current Hole", 1);

		players.clear();
		KolfGame::scoresFromSaved(&config, players);
		if (!isTutorial)
			Kolf::Ui::HomeView::rememberSavedGame(loadedGame);
	}

	launchGame(firstHole);
}

void KolfWindow::showSetup(const QString &lockedCourse)
{
	if (!setupView) {
		setupView = new Kolf::Ui::NewGameView(applicationStack);
		applicationStack->addWidget(setupView);
		connect(setupView, &Kolf::Ui::NewGameView::backRequested, this, [this] {
			filename = QString();
			applicationStack->setCurrentWidget(dummy);
		});
		connect(setupView, &Kolf::Ui::NewGameView::highScoresRequested, this, [this](const QString &course) {
			if (course.isEmpty())
				return;
			CourseInfo courseInfo;
			KolfGame::courseInfo(courseInfo, course);
			auto *scoreDialog = new KGameHighScoreDialog(KGameHighScoreDialog::Name | KGameHighScoreDialog::Custom1 | KGameHighScoreDialog::Score, this);
			scoreDialog->setAttribute(Qt::WA_DeleteOnClose);
			scoreDialog->addField(KGameHighScoreDialog::Custom1, i18n("Par"), QStringLiteral("Par"));
			scoreDialog->setConfigGroup(qMakePair(QByteArray(courseInfo.untranslatedName.toUtf8() + " Highscores"), i18n("High Scores for %1", courseInfo.name)));
			scoreDialog->setComment(i18n("High Scores for %1", courseInfo.name));
			scoreDialog->show();
		});
		connect(setupView, &Kolf::Ui::NewGameView::startRequested, this,
			[this](const QList<Kolf::Ui::LocalPlayer> &chosen, const QString &course, bool strict) {
				applicationStack->setCurrentWidget(dummy);
				isTutorial = false;
				loadedGame = QString();
				filename = course;
				competition = strict;
				players.clear();
				int id = 1;
				for (const auto &entry : chosen) {
					players.append(Player());
					players.last().setName(entry.name);
					players.last().ball()->setColor(entry.color);
					players.last().setId(id++);
				}
				launchGame(1);
			});
	}
	setupView->prepare(lockedCourse);
	applicationStack->setCurrentWidget(setupView);
}

void KolfWindow::launchGame(int firstHole)
{
	if (!filename.isEmpty()) {
		const QString problem = KolfGame::coursePlayProblem(filename);
		if (!problem.isEmpty()) {
			KMessageBox::error(this, problem, i18nc("@title:window", "Course Cannot Be Played"));
			filename = QString();
			closeGame();
			return;
		}
	}
	homeView->hide();
	delete scoreboard;
	scoreboard = new ScoreBoard(dummy);
	layout->addWidget(scoreboard, 2, 0);
	scoreboard->show();

	for (PlayerList::Iterator it = players.begin(); it != players.end(); ++it)
		scoreboard->newPlayer((*it).name());

	delete spacer;
        spacer = nullptr;
	delete game;
        game = new KolfGame(&players, filename, dummy);
	game->setStrict(competition);
	if (m_savedRandomSeed >= 0)
		game->setRandomSeed(quint32(m_savedRandomSeed));
	m_savedRandomSeed = -1;

	connect(game, &KolfGame::newHole, scoreboard, &ScoreBoard::newHole);
	connect(game, &KolfGame::scoreChanged, scoreboard, &ScoreBoard::setScore);
	connect(game, &KolfGame::parChanged, scoreboard, &ScoreBoard::parChanged);
	connect(game, &KolfGame::modifiedChanged, this, &KolfWindow::updateModified);
	connect(game, &KolfGame::newPlayersTurn, this, &KolfWindow::newPlayersTurn);
	connect(game, &KolfGame::saveGame, this, &KolfWindow::autoSaveGame);
	connect(game, &KolfGame::saveGameAs, this, &KolfWindow::saveGameAs);
	connect(game, &KolfGame::holesDone, this, &KolfWindow::gameOver);
	connect(game, &KolfGame::leaveRequested, this, &KolfWindow::returnHome);
	connect(game, &KolfGame::newPlayersTurn, this, &KolfWindow::updateHud);
	connect(game, &KolfGame::scoreChanged, this, &KolfWindow::updateHud);
	connect(game, &KolfGame::parChanged, this, &KolfWindow::updateHud);
	connect(game, &KolfGame::largestHole, this, &KolfWindow::updateHud);
	connect(game, qOverload<int>(&KolfGame::currentHole), this, &KolfWindow::updateHud);
	connect(game, &KolfGame::inPlayStart, this, [this] {
		if (!gameHud->findChild<QWidget *>(QStringLiteral("hudHint"))->isVisible()) return;
		gameHud->clearHint();
		KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
		settings.writeEntry("firstShotHintShown", true);
		settings.sync();
	});
	connect(game, &KolfGame::checkEditing, this, &KolfWindow::checkEditing);
	connect(game, &KolfGame::editingStarted, this, &KolfWindow::editingStarted);
	connect(game, &KolfGame::editingEnded, this, &KolfWindow::editingEnded);
	connect(game, &KolfGame::inPlayStart, this, &KolfWindow::inPlayStart);
	connect(game, &KolfGame::inPlayEnd, this, &KolfWindow::inPlayEnd);
	connect(game, &KolfGame::maxStrokesReached, this, &KolfWindow::maxStrokesReached);
	connect(game, &KolfGame::largestHole, this, &KolfWindow::updateHoleMenu);
	connect(game, &KolfGame::titleChanged, this, &KolfWindow::titleChanged);
	connect(game, &KolfGame::newStatusText, this, &KolfWindow::newStatusText);
        connect(game, qOverload<int>(&KolfGame::currentHole), this,
                &KolfWindow::setCurrentHole);
        connect(holeAction, &KSelectAction::indexTriggered, game,
                [offlineGame = game](int index) { offlineGame->switchHole(index + 1); });
        connect(nextAction, &QAction::triggered, game, &KolfGame::nextHole);
	connect(prevAction, &QAction::triggered, game, &KolfGame::prevHole);
	connect(firstAction, &QAction::triggered, game, &KolfGame::firstHole);
	connect(lastAction, &QAction::triggered, game, &KolfGame::lastHole);
	connect(randAction, &QAction::triggered, game, &KolfGame::randHole);
	connect(editingAction, &QAction::triggered, game, &KolfGame::toggleEditMode);
	connect(newHoleAction, &QAction::triggered, game, &KolfGame::addNewHole);
	connect(clearHoleAction, &QAction::triggered, game, &KolfGame::clearHole);
	connect(resetHoleAction, &QAction::triggered, game, &KolfGame::resetHole);
	connect(undoShotAction, &QAction::triggered, game, &KolfGame::undoShot);
	//connect(replayShotAction, &QAction::triggered, game, &KolfGame::replay);
	connect(aboutAction, &QAction::triggered, game, &KolfGame::showInfoDlg);
	connect(useMouseAction, &QAction::toggled, game, &KolfGame::setUseMouse);
	connect(useAdvancedPuttingAction, &QAction::toggled, game, &KolfGame::setUseAdvancedPutting);
	connect(soundAction, &QAction::toggled, game, &KolfGame::setSound);
	connect(showGuideLineAction, &QAction::toggled, game, &KolfGame::setShowGuideLine);
	connect(showInfoAction, &QAction::toggled, game, &KolfGame::setShowInfo);

	game->setUseMouse(useMouseAction->isChecked());
	game->setUseAdvancedPutting(useAdvancedPuttingAction->isChecked());
	game->setShowInfo(showInfoAction->isChecked());
	game->setShowGuideLine(showGuideLineAction->isChecked());
	game->setSound(soundAction->isChecked());

	layout->addWidget(game, 1, 0);//, Qt::AlignCenter);

	game->show();
	game->setFocus();

	setEditingEnabled(true);
	endAction->setEnabled(true);
	setHoleMovementEnabled(true);
	setHoleOtherEnabled(true);
	aboutAction->setEnabled(true);
	highScoreAction->setEnabled(true);
	saveAction->setEnabled(true);
	saveAsAction->setEnabled(true);
	saveGameAction->setEnabled(true);
	saveGameAsAction->setEnabled(true);

	clearHoleAction->setEnabled(false);
	newHoleAction->setEnabled(false);
	newAction->setEnabled(false);
	loadGameAction->setEnabled(false);
	tutorialAction->setEnabled(false);

	// so game can do stuff that needs to be done
	// after things above are connected
	game->startFirstHole(firstHole);
	gameHud->clearHint();
	const KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
	if (!settings.readEntry("firstShotHintShown", false))
		gameHud->setHint(useAdvancedPuttingAction->isChecked()
			? i18n("Aim with the mouse or the Left and Right arrows. Press once to start, again to set power, and a third time as the meter swings through the middle.")
			: i18n("Aim with the mouse or the Left and Right arrows. Hold the mouse button or the Down arrow to build power, then release to putt."));
	gameHud->show();
	updateHud();
}

void KolfWindow::updateHud()
{
	if (!game || !gameHud)
		return;
	const int hole = game->currentHole();
	if (editingAction->isChecked()) {
		gameHud->setEditing(hole, game->holeCount());
		return;
	}
	gameHud->setHole(hole, game->holeCount(), game->holePar());
	if (!game->hasCurrentPlayer()) {
		gameHud->setMessage(QString());
		return;
	}
	gameHud->setTurn(game->currentPlayerName(), game->currentPlayerColor(), game->currentPlayerStrokes() + 1);
}

void KolfWindow::editNewCourse()
{
	if (game) {
		if (game->askSave(true))
			return;
		game->pause();
	}
	isTutorial = false;
	loadedGame = QString();
	filename = QString();
	competition = false;
	players.clear();
	players.append(Player());
	players.last().ball()->setColor(Qt::white);
	players.last().setName(i18n("Course Designer"));
	players.last().setId(1);
	launchGame(1);
	if (!editingAction->isChecked())
		editingAction->trigger();
}

void KolfWindow::continueGame(const QString &savedGame)
{
	if (savedGame.isEmpty() || !QFileInfo(savedGame).isFile())
		return;
	if (game) {
		if (game->askSave(true))
			return;
		game->pause();
	}
	filename = QString();
	loadedGame = savedGame;
	isTutorial = false;
	startNewGame();
}

void KolfWindow::returnHome()
{
	if (onlineModeActive) {
		if (onlineMatchController && KMessageBox::warningTwoActions(this,
			i18n("Leaving this online match will interrupt it for everyone. Return to the main menu?"),
			i18nc("@title:window", "Leave Online Match?"),
			KGuiItem(i18nc("@action:button", "Leave Match"), QStringLiteral("network-disconnect")),
			KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction)
			return;
		leaveOnline();
		return;
	}
	if (applicationStack->currentWidget() != dummy) {
		if (applicationStack->currentWidget() == setupView)
			filename = QString();
		applicationStack->setCurrentWidget(dummy);
		return;
	}
	if (game && !editingAction->isChecked() && KMessageBox::warningContinueCancel(this,
		i18n("Leave this game and return to the main menu? Progress since your last save will be lost."),
		i18nc("@title:window", "Return to Main Menu?"),
		KGuiItem(i18nc("@action:button", "Return to Main Menu"), QStringLiteral("go-home")),
		KStandardGuiItem::cancel(), QStringLiteral("confirmReturnToMainMenu")) != KMessageBox::Continue)
		return;
	closeGame();
}

void KolfWindow::newGame()
{
	isTutorial = false;
	filename = QString();
	startNewGame();
}

void KolfWindow::tutorial()
{
	QString newfilename = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("tutorial.kolfgame"));
	if (newfilename.isNull())
	        return;

	filename = QString();
	loadedGame = newfilename;
	isTutorial = true;

	startNewGame();

	loadedGame = QString();
}

void KolfWindow::closeGame()
{
	if (game)
	{
		if (game->askSave(true))
			return;
		game->pause();
	}

	filename = QString();

	editingEnded();
	delete game;
	game = nullptr;
	gameHud->hide();
	delete scoreboard;
	scoreboard = nullptr;
	loadedGame = QString();

	editingAction->setChecked(false);
	setEditingEnabled(false);
	endAction->setEnabled(false);
	aboutAction->setEnabled(false);
	highScoreAction->setEnabled(false);
	saveAction->setEnabled(false);
	saveAsAction->setEnabled(false);
	saveGameAction->setEnabled(false);
	saveGameAsAction->setEnabled(false);
	setHoleMovementEnabled(false);
	setHoleOtherEnabled(false);

	clearHoleAction->setEnabled(false);
	newHoleAction->setEnabled(false);
	newAction->setEnabled(true);
	loadGameAction->setEnabled(true);
	tutorialAction->setEnabled(true);

	titleChanged(QString());
	updateModified(false);

	homeView->refresh();
	homeView->show();
	homeView->raise();
	QTimer::singleShot(100, this, &KolfWindow::createSpacer);
}

void KolfWindow::createSpacer()
{
	// make a player to play the spacer hole
	spacerPlayers.clear();
	spacerPlayers.append(Player());
	spacerPlayers.last().ball()->setColor(Qt::yellow);
	spacerPlayers.last().setName(QStringLiteral("player"));
	spacerPlayers.last().setId(1);

	delete spacer;
	spacer = new KolfGame(&spacerPlayers, QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("intro")), dummy);
	spacer->setSound(false);
	layout->addWidget(spacer, 1, 0);//, Qt::AlignCenter);
	spacer->ignoreEvents(true);

	spacer->show();
	spacer->startFirstHole(1);
	spacer->hidePutter();
	homeView->setAnchor(spacer);
	if (!game)
		homeView->raise();
}

void KolfWindow::gameOver()
{
	int curPar = 0;
	int lowScore = INT_MAX; // let's hope it doesn't stay this way!
	int curScore = 1;

	// names of people who had the lowest score
	QStringList names;

	HighScoreList highScores;
	int scoreBoardIndex = 1;

	while (curScore != 0)
	{
		QString curName;

		// name taken as a reference and filled out
		curScore = scoreboard->total(scoreBoardIndex, curName);

		scoreBoardIndex++;

		if (curName == i18n("Par"))
		{
			curPar = curScore;
			curScore = 0;
			continue;
		}

		if (curScore == 0)
			continue;

		// attempt to add everybody to the highscore list
		// (ignored if we aren't competing down below)
		highScores.append(HighScore(curName, curScore));

		if (curScore < lowScore)
		{
			names.clear();
			lowScore = curScore;
			names.append(curName);
		}
		else if (curScore == lowScore)
			names.append(curName);
	}

	// only announce a winner if more than two entries
	// (player and par) are on the scoreboard + one to go past end
	// + 1 for koodoo
	showLocalResults();

	if (competition)
	{
		// deal with highscores
		// KGameHighScoreDialog makes it very easy :-))

		KGameHighScoreDialog *scoreDialog = new KGameHighScoreDialog(KGameHighScoreDialog::Name | KGameHighScoreDialog::Custom1 | KGameHighScoreDialog::Score, this);
		scoreDialog->addField(KGameHighScoreDialog::Custom1, i18n("Par"), QStringLiteral("Par"));

		CourseInfo courseInfo;
		game->courseInfo(courseInfo, game->curFilename());

		scoreDialog->setConfigGroup(qMakePair(QByteArray(courseInfo.untranslatedName.toUtf8() + " Highscores"), i18n("High Scores for %1", courseInfo.name)));

		for (HighScoreList::Iterator it = highScores.begin(); it != highScores.end(); ++it)
		{
			KGameHighScoreDialog::FieldInfo info;
                        info[KGameHighScoreDialog::Name] = (*it).name;
                        info[KGameHighScoreDialog::Score].setNum((*it).score);
			info[KGameHighScoreDialog::Custom1] = QString::number(curPar);

                        scoreDialog->addScore(info, KGameHighScoreDialog::LessIsMore);
		}

		scoreDialog->setComment(i18n("High Scores for %1", courseInfo.name));
		scoreDialog->show();
	}

	QTimer::singleShot(700, this, &KolfWindow::closeGame);
}

void KolfWindow::showLocalResults()
{
	if (!game || !scoreboard)
		return;
	QList<Kolf::Results::PlayerInfo> results;
	lastPlayers.clear();
	for (const auto &player : std::as_const(players)) {
		results.append({player.name(), player.ball()->color(), player.scores()});
		lastPlayers.append({player.name(), player.ball()->color()});
	}
	lastCourse = game->curFilename();
	lastCompetition = competition;
	if (!resultsPage) {
		resultsPage = new QWidget(applicationStack);
		auto *pageLayout = new QVBoxLayout(resultsPage);
		localResults = new Kolf::Ui::ResultsView(resultsPage);
		pageLayout->addWidget(localResults, 1);
		auto *buttons = new QHBoxLayout;
		auto *home = new QPushButton(QIcon::fromTheme(QStringLiteral("go-home")), i18nc("@action:button", "Main Menu"), resultsPage);
		home->setObjectName(QStringLiteral("resultsHome"));
		auto *again = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:button", "Play Again"), resultsPage);
		again->setObjectName(QStringLiteral("resultsPlayAgain"));
		again->setDefault(true);
		buttons->addWidget(home);
		buttons->addStretch();
		buttons->addWidget(again);
		pageLayout->addLayout(buttons);
		applicationStack->addWidget(resultsPage);
		connect(home, &QPushButton::clicked, this, [this] { applicationStack->setCurrentWidget(dummy); });
		connect(again, &QPushButton::clicked, this, &KolfWindow::playAgain);
	}
	CourseInfo info;
	KolfGame::courseInfo(info, lastCourse);
	localResults->setSummary(Kolf::Results::summarizeLocalGame(
		info.name.isEmpty() ? game->courseName() : info.name, results, scoreboard->pars()));
	applicationStack->setCurrentWidget(resultsPage);
	resultsPage->findChild<QPushButton *>(QStringLiteral("resultsPlayAgain"))->setFocus();
}

void KolfWindow::playAgain()
{
	if (lastPlayers.isEmpty())
		return;
	applicationStack->setCurrentWidget(dummy);
	if (game) {
		if (game->askSave(true))
			return;
		game->pause();
	}
	isTutorial = false;
	loadedGame = QString();
	filename = lastCourse;
	competition = lastCompetition;
	players.clear();
	int id = 1;
	for (const auto &entry : std::as_const(lastPlayers)) {
		players.append(Player());
		players.last().setName(entry.first);
		players.last().ball()->setColor(entry.second);
		players.last().setId(id++);
	}
	launchGame(1);
}

void KolfWindow::showHighScores()
{
	KGameHighScoreDialog *scoreDialog = new KGameHighScoreDialog(KGameHighScoreDialog::Name | KGameHighScoreDialog::Custom1 | KGameHighScoreDialog::Score, this);
	scoreDialog->addField(KGameHighScoreDialog::Custom1, i18n("Par"), QStringLiteral("Par"));

	CourseInfo courseInfo;
	game->courseInfo(courseInfo, game->curFilename());

	scoreDialog->setConfigGroup(qMakePair(QByteArray(courseInfo.untranslatedName.toUtf8() + " Highscores"), i18n("High Scores for %1", courseInfo.name)));
	scoreDialog->setComment(i18n("High Scores for %1", courseInfo.name));
	scoreDialog->show();
}

void KolfWindow::save()
{
	if (filename.isNull())
	{
		saveAs();
		return;
	}

	if (game) {
		game->save();
		game->setFocus();
	}
}

void KolfWindow::saveAs()
{
	QPointer<QFileDialog> fileSaveDialog = new QFileDialog(this);
	fileSaveDialog->setWindowTitle(i18nc("@title:window", "Pick Kolf Course to Save To"));
	fileSaveDialog->setMimeTypeFilters(QStringList(QStringLiteral("application/x-kourse")));
	fileSaveDialog->setAcceptMode(QFileDialog::AcceptSave);
	if (fileSaveDialog->exec() == QDialog::Accepted) {
		QUrl newfile = fileSaveDialog->selectedUrls().first();
		if (!newfile.isEmpty()) {
			filename = newfile.toLocalFile();
			game->setFilename(filename);
			game->save();
			game->setFocus();
		}
	}
	delete fileSaveDialog;
}

void KolfWindow::saveGameAs()
{
		QPointer<QFileDialog> fileSaveDialog = new QFileDialog(this);
		fileSaveDialog->setWindowTitle(i18nc("@title:window", "Pick Saved Game to Save To"));
		fileSaveDialog->setMimeTypeFilters(QStringList(QStringLiteral("application/x-kolf")));
		fileSaveDialog->setAcceptMode(QFileDialog::AcceptSave);
		if (fileSaveDialog->exec() == QDialog::Accepted) {
			QUrl newfile = fileSaveDialog->selectedUrls().first();
			if (newfile.isEmpty()) {
				return;
			}
			else {
				loadedGame = newfile.toLocalFile();
				saveGame();
			}
		}
		delete fileSaveDialog;
}

void KolfWindow::saveGame()
{
	if (loadedGame.isNull())
	{
		saveGameAs();
		return;
	}

	writeSavedGame(loadedGame);
}

void KolfWindow::writeSavedGame(const QString &path)
{
	KConfig config(path, KConfig::SimpleConfig);
	KConfigGroup configGroup(config.group(QStringLiteral("0 Saved Game")));

	configGroup.writeEntry("Competition", competition);
	configGroup.writeEntry("Course", filename);

	game->saveScores(&config);

	configGroup.sync();
	if (!isTutorial)
		Kolf::Ui::HomeView::rememberSavedGame(path);
}

void KolfWindow::autoSaveGame()
{
	if (!game || isTutorial || editingAction->isChecked() || filename.isEmpty())
		return;
	QString path = loadedGame;
	if (path.isEmpty()) {
		const QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
		directory.mkpath(QStringLiteral("."));
		const QString configName = QFileInfo(KSharedConfig::openConfig()->name()).fileName();
		path = directory.filePath(configName == QLatin1String("kolfrc") ? QStringLiteral("autosave.kolfgame")
			: configName + QStringLiteral("-autosave.kolfgame"));
	}
	writeSavedGame(path);
}

void KolfWindow::loadGame()
{
	QPointer<QFileDialog> fileLoadDialog = new QFileDialog(this);
	fileLoadDialog->setWindowTitle(i18nc("@title:window", "Pick Kolf Saved Game"));
	fileLoadDialog->setMimeTypeFilters(QStringList(QStringLiteral("application/x-kolf")));
	fileLoadDialog->setAcceptMode(QFileDialog::AcceptOpen);
	fileLoadDialog->setFileMode(QFileDialog::ExistingFile);
	if (fileLoadDialog->exec() == QDialog::Accepted) {
		loadedGame = fileLoadDialog->selectedUrls().first().toLocalFile();
		if (loadedGame.isEmpty()) {
			return;
		}
		else {
			isTutorial = false;
			startNewGame();
		}
	}
	delete fileLoadDialog;
}

// called by main for command line files
void KolfWindow::openUrl(const QUrl &url)
{
	QString path;
	if (url.isLocalFile()) {
		path = url.toLocalFile();
	} else {
		auto *download = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/kolf-XXXXXX.") + QFileInfo(url.path()).suffix(), this);
		if (!download->open()) {
			closeGame();
			return;
		}
		path = download->fileName();
		download->close();
		KIO::FileCopyJob *job = KIO::file_copy(url, QUrl::fromLocalFile(path), -1, KIO::Overwrite);
		KJobWidgets::setWindow(job, this);
		if (!job->exec()) {
			closeGame();
			return;
		}
	}

	isTutorial = false;
	const QString mimeType = QMimeDatabase().mimeTypeForFile(path).name();
	bool savedGame = mimeType == QLatin1String("application/x-kolf");
	bool course = mimeType == QLatin1String("application/x-kourse");
	if (!savedGame && !course) {
		QFile file(path);
		const QByteArray head = file.open(QIODevice::ReadOnly) ? file.read(64 * 1024) : QByteArray();
		savedGame = head.contains("[0 Saved Game]");
		course = !savedGame && head.contains("-hole@");
	}
	if (course)
		filename = path;
	else if (savedGame)
		loadedGame = path;
	else {
		closeGame();
		return;
	}

	QTimer::singleShot(10, this, &KolfWindow::startNewGame);
}

void KolfWindow::newPlayersTurn(Player *player)
{
	tempStatusBarText = i18n("%1's turn", player->name());

	if (showInfoAction->isChecked())
		statusBar()->showMessage(tempStatusBarText, 5 * 1000);
	else
		statusBar()->showMessage(tempStatusBarText);

	scoreboard->setCurrentCell(player->id() - 1, game->currentHole() - 1);
}

void KolfWindow::newStatusText(const QString &text)
{
	if (text.isEmpty())
		statusBar()->showMessage(tempStatusBarText);
	else
		statusBar()->showMessage(text);
}

void KolfWindow::editingStarted()
{
	delete editor;
	editor = new Editor(dummy);
	editor->setObjectName( QStringLiteral( "Editor" ) );
	connect(editor, &Editor::addNewItem, game, &KolfGame::addNewObject);
	connect(editor, &Editor::changed, game, &KolfGame::setModified);
	connect(editor, &Editor::addNewItem, this, &KolfWindow::setHoleFocus);
	connect(game, &KolfGame::newSelectedItem, editor, &Editor::setItem);
	connect(editor, &Editor::duplicateRequested, game, &KolfGame::duplicateSelected);
	editor->setHistory(game->editHistory());
	connect(game, &KolfGame::holeProblemsChanged, editor, &Editor::setProblems);
	editor->setProblems(game->holeProblems());
	connect(editor, &Editor::testRequested, this, [this] {
		game->beginTest();
		editingAction->trigger();
	});

	scoreboard->hide();
	gameHud->clearHint();

	layout->addWidget(editor, 2, 0);
	editor->show();

	clearHoleAction->setEnabled(true);
	newHoleAction->setEnabled(true);
	setCourseEditingActionsVisible(true);
	setHoleOtherEnabled(false);
	updateHud();

	game->setFocus();
}

void KolfWindow::editingEnded()
{
	delete editor;
	editor = nullptr;

	if (scoreboard)
		scoreboard->show();

	clearHoleAction->setEnabled(false);
	newHoleAction->setEnabled(false);
	setCourseEditingActionsVisible(false);
	setHoleOtherEnabled(true);
	updateHud();

	if (game)
		game->setFocus();
}

void KolfWindow::inPlayStart()
{
	setEditingEnabled(false);
	setHoleOtherEnabled(false);
	setHoleMovementEnabled(false);
}

void KolfWindow::inPlayEnd()
{
	setEditingEnabled(true);
	setHoleOtherEnabled(true);
	setHoleMovementEnabled(true);
}

void KolfWindow::maxStrokesReached(const QString &name)
{
	KMessageBox::information(this, i18n("%1's score has reached the maximum for this hole.", name));
}

void KolfWindow::updateHoleMenu(int largest)
{
	QStringList items;
	for (int i = 1; i <= largest; ++i)
		items.append(QString::number(i));

	// setItems for some reason enables the action
	bool shouldbe = holeAction->isEnabled();
	holeAction->setItems(items);
	holeAction->setEnabled(shouldbe);
}

void KolfWindow::setHoleMovementEnabled(bool yes)
{
	if (competition)
		yes = false;

	holeAction->setEnabled(yes);

	nextAction->setEnabled(yes);
	prevAction->setEnabled(yes);
	firstAction->setEnabled(yes);
	lastAction->setEnabled(yes);
	randAction->setEnabled(yes);
}

void KolfWindow::setHoleOtherEnabled(bool yes)
{
	if (competition)
		yes = false;

	resetHoleAction->setEnabled(yes);
	undoShotAction->setEnabled(yes);
	//replayShotAction->setEnabled(yes);
}

void KolfWindow::setEditingEnabled(bool yes)
{
	editingAction->setEnabled(competition? false : yes);
}

void KolfWindow::checkEditing()
{
	editingAction->setChecked(true);
}

void KolfWindow::updateModified(bool mod)
{
	courseModified = mod;
	titleChanged(title);
}

void KolfWindow::titleChanged(const QString &newTitle)
{
	title = newTitle;
	setCaption(title, courseModified);
}

void KolfWindow::useMouseChanged(bool yes)
{
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings")); configGroup.writeEntry("useMouse", yes); configGroup.sync();
}

void KolfWindow::useAdvancedPuttingChanged(bool yes)
{
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings")); configGroup.writeEntry("useAdvancedPutting", yes); configGroup.sync();
}

void KolfWindow::showInfoChanged(bool yes)
{
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings")); configGroup.writeEntry("showInfo", yes); configGroup.sync();
}

void KolfWindow::showGuideLineChanged(bool yes)
{
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings")); configGroup.writeEntry("showGuideLine", yes); configGroup.sync();
}

void KolfWindow::soundChanged(bool yes)
{
	KConfigGroup configGroup(KSharedConfig::openConfig(), QStringLiteral("Settings")); configGroup.writeEntry("sound", yes); configGroup.sync();
}

void KolfWindow::enableAllMessages()
{
	KMessageBox::enableAllMessages();
}

void KolfWindow::setCourseEditingActionsVisible(bool visible)
{
	for (QAction *action : {newHoleAction, clearHoleAction, saveAction, saveAsAction})
		action->setVisible(visible);
}

void KolfWindow::showSettings()
{
	openSettings(false);
}

void KolfWindow::showServerSettings()
{
	openSettings(true);
}

void KolfWindow::openSettings(bool serversPage)
{
	KConfigGroup online(KSharedConfig::openConfig(), QStringLiteral("Online"));
	Kolf::Ui::Preferences current;
	current.advancedPutting = useAdvancedPuttingAction->isChecked();
	current.useMouse = useMouseAction->isChecked();
	current.confirmLeaveGame = KMessageBox::shouldBeShownContinue(QStringLiteral("confirmReturnToMainMenu"));
	current.showInfo = showInfoAction->isChecked();
	current.showGuideLine = showGuideLineAction->isChecked();
	current.sound = soundAction->isChecked();
	current.volume = Kolf::Effects::volume();
	current.confirmHostActions = online.readEntry("confirmHostActions", true);
	current.onlineName = online.readEntry("displayName", QStringLiteral("Player"));
	current.onlineColorMode = online.readEntry("colorMode", QStringLiteral("auto"));
	current.onlineCustomColor = online.readEntry("customColor", QStringLiteral("#0072b2ff"));
	current.servers = Kolf::Online::ServerList::load(online);

	QPointer<Kolf::Ui::SettingsDialog> dialog = new Kolf::Ui::SettingsDialog(current, this);
	connect(dialog, &Kolf::Ui::SettingsDialog::resetMessagesRequested, this, &KolfWindow::enableAllMessages);
	if (serversPage)
		dialog->showServersPage();
	if (dialog->exec() == QDialog::Accepted && dialog) {
		const auto chosen = dialog->preferences();
		useAdvancedPuttingAction->setChecked(chosen.advancedPutting);
		useMouseAction->setChecked(chosen.useMouse);
		showInfoAction->setChecked(chosen.showInfo);
		showGuideLineAction->setChecked(chosen.showGuideLine);
		soundAction->setChecked(chosen.sound);
		Kolf::Effects::setVolume(chosen.volume);
		if (chosen.confirmLeaveGame)
			KMessageBox::enableMessage(QStringLiteral("confirmReturnToMainMenu"));
		else
			KMessageBox::saveDontShowAgainContinue(QStringLiteral("confirmReturnToMainMenu"));
		online.writeEntry("confirmHostActions", chosen.confirmHostActions);
		online.writeEntry("displayName", chosen.onlineName);
		online.writeEntry("colorMode", chosen.onlineColorMode);
		online.writeEntry("customColor", chosen.onlineCustomColor);
		chosen.servers.save(online);
		online.sync();
		if (onlineWidget) {
			onlineWidget->setProfile(chosen.onlineName, chosen.onlineColorMode, chosen.onlineCustomColor);
			onlineWidget->serversChanged();
		}
		if (onlineMatchView)
			onlineMatchView->setConfirmHostActions(chosen.confirmHostActions);
	}
	delete dialog;
}

void KolfWindow::setCurrentHole(int hole)
{
	if (!holeAction || holeAction->items().count() < hole)
		return;
	// Golf is 1-based, KListAction is 0-based
	holeAction->setCurrentItem(hole - 1);
}

#include "moc_kolf.cpp"
