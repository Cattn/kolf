// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "serverlist.h"
#include "serverprobe.h"

#include <QHash>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace Kolf::Online {

class ServerSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit ServerSettingsPage(const ServerList &servers, QWidget *parent = nullptr);
    ServerList serverList() const { return m_servers; }

protected:
    void showEvent(QShowEvent *event) override;

private:
    void rebuild(const QString &selectUrl = {});
    QString selectedUrl() const;
    void updateButtons();
    void updateDetails();
    void addServer();
    void editServer();
    void removeServer();
    void setDefaultServer();
    void testServers(const QStringList &urls);
    void startNextProbe();
    bool editDialog(const QString &title, QString &name, QString &address, const QString &currentUrl);

    ServerList m_servers;
    QTreeWidget *m_list;
    QLabel *m_details;
    QPushButton *m_add;
    QPushButton *m_edit;
    QPushButton *m_remove;
    QPushButton *m_setDefault;
    QPushButton *m_test;
    ServerProbe m_probe;
    QStringList m_probeQueue;
    QHash<QString, ProbeResult> m_results;
    bool m_testedOnShow = false;
};

}
