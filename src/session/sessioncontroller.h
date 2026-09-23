// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gamesessionadapter.h"
#include "net/networkclient.h"
#include "replication/presentationcontroller.h"
#include "itemfactory.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QSet>
class QWidget;
class QGraphicsPathItem;

namespace Kolf::Session {
class SessionController : public QObject {
    Q_OBJECT
public:
    explicit SessionController(const QJsonObject &config, Net::NetworkClient *network, QWidget *gameHost, QObject *parent = nullptr);
    ~SessionController() override;

public Q_SLOTS:
    void requestResync();
    void chooseDrop();
    void chooseRehit();
    void setHostControlsEnabled(bool enabled);
    void resetOnlineHole();
    void undoOnlineShot();
    void setUseMouse(bool enabled);
    void setUseAdvancedPutting(bool enabled);
    void setSound(bool enabled);
    void setShowInfo(bool enabled);
    void setShowGuideLine(bool enabled);

Q_SIGNALS:
    void gameReady(KolfGame *game);
    void statusChanged(const QString &status);
    void noticeChanged(const QString &notice);
    void scorecardChanged(const QJsonArray &scores, const QJsonArray &pars, int activePlayer, int currentHole);
    void hazardChoiceChanged(bool available);
    void hostControlsChanged(bool enabled, bool canToggle, bool canReset, bool canUndo);

private:
    void receive(const QJsonObject &message);
    void load();
    void submit(const ShotIntent &intent);
    void commit(const QString &phase);
    void interrupt(const QString &reason);
    void refresh();
    void log(const QString &event, QJsonObject data = {});
    QJsonObject wireMessage(const QString &type, QJsonObject payload = {}) const;
    void send(const QString &type, QJsonObject payload = {}, bool visual = false);
    QJsonObject unwrap(const QJsonObject &message) const;
    QJsonObject state() const;
    bool ownsSlot(int slot) const;
    QString playerIdForSlot(int slot) const;
    void chooseHazardAction(const QString &action);
    void sendAim();
    void showRemoteAim(const QJsonObject &aim);
    void clearRemoteAim();
    QJsonObject m_config;
    Role m_role;
    Kolf::ItemFactory m_factory;
    PlayerList m_players;
    KolfGame *m_game = nullptr;
    GameSessionAdapter *m_adapter = nullptr;
    Net::NetworkClient *m_network;
    QWidget *m_gameHost;
    Replication::PresentationController m_presentation;
    QTimer m_frames;
    QTimer m_aimTimer;
    QFile m_log;
    QElapsedTimer m_clock;
    QElapsedTimer m_pendingClock;
    QString m_hash;
    QString m_phase = QStringLiteral("Loading");
    QString m_pending;
    QString m_choiceId;
    int m_choiceSlot = -1;
    int m_revision = 0, m_generation = 1, m_turn = 1, m_lastHole = 1;
    int m_syncId = 0;
    int m_frameSeq = 0, m_receivedFrame = 0;
    bool m_ready = false, m_interrupted = false, m_awaitingResync = false;
    bool m_hostControlsEnabled = false, m_hostActionPending = false;
    QString m_hostTogglePending;
    QString m_hostResetPending;
    QString m_hostUndoPending;
    bool m_testFaultScheduled = false;
    bool m_scriptedHostResetDone = false;
    bool m_scriptedHostUndoDone = false;
    QJsonObject m_pendingMessage;
    QJsonObject m_lastAim;
    QGraphicsPathItem *m_remoteAim = nullptr;
    QTimer m_retry;
    qint64 m_frameBytes = 0;
    qint64 m_frameCount = 0;
    QHash<QString, QJsonObject> m_admitted;
    QSet<int> m_scriptedTurns;
    QSet<QString> m_localPlayerIds;
    QJsonArray m_pars;
    bool m_useMouse = true;
    bool m_useAdvancedPutting = false;
    bool m_sound = true;
    bool m_showInfo = true;
    bool m_showGuideLine = true;
};
}
