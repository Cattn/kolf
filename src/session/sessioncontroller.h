// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gamesessionadapter.h"
#include "net/networkclient.h"
#include "replication/presentationcontroller.h"
#include "itemfactory.h"
#include <QElapsedTimer>
#include <QFile>
#include <QWidget>
#include <QTimer>
#include <QSet>
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace Kolf::Session {
class SessionController : public QWidget {
    Q_OBJECT
public:
    explicit SessionController(const QJsonObject &config);
    ~SessionController() override;
private:
    void receive(const QJsonObject &message);
    void load();
    void submit(const ShotIntent &intent);
    void commit(const QString &phase);
    void interrupt(const QString &reason);
    void refresh();
    void log(const QString &event, QJsonObject data = {});
    QJsonObject state() const;
    QJsonObject m_config;
    Role m_role;
    Kolf::ItemFactory m_factory;
    PlayerList m_players;
    KolfGame *m_game = nullptr;
    GameSessionAdapter *m_adapter = nullptr;
    Net::NetworkClient m_network;
    Replication::PresentationController m_presentation;
    QVBoxLayout *m_layout;
    QLabel *m_status;
    QLabel *m_notice;
    QPushButton *m_drop;
    QPushButton *m_rehit;
    QTableWidget *m_scores;
    QTimer m_frames;
    QFile m_log;
    QElapsedTimer m_clock;
    QElapsedTimer m_pendingClock;
    QString m_hash;
    QString m_phase = QStringLiteral("Loading");
    QString m_pending;
    QString m_choiceId;
    int m_choiceSlot = -1;
    int m_revision = 0, m_generation = 1, m_turn = 1, m_lastHole = 1, m_slot = -1;
    int m_frameSeq = 0, m_receivedFrame = 0;
    bool m_ready = false, m_interrupted = false;
    bool m_testFaultScheduled = false;
    QJsonObject m_pendingMessage;
    QTimer m_retry;
    qint64 m_frameBytes = 0;
    qint64 m_frameCount = 0;
    QHash<QString, QJsonObject> m_admitted;
    QSet<int> m_scriptedTurns;
};
}
