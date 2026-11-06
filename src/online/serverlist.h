// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QUrl>

class KConfigGroup;

namespace Kolf::Online {

struct ServerAddress {
    QUrl url;
    QString error;
    bool isValid() const { return error.isEmpty() && url.isValid(); }
};

bool isPrivateHost(const QString &host);
ServerAddress normalizeServerAddress(const QString &text);
bool isNewerClientVersion(const QString &candidate, const QString &current);

struct ServerEntry {
    QString name;
    QString url;
    bool builtIn = false;
};

class ServerList {
public:
    static ServerEntry builtInServer();
    static ServerList load(KConfigGroup &group);
    void save(KConfigGroup &group) const;

    QList<ServerEntry> servers() const;
    int indexOf(const QString &url) const;
    QString defaultUrl() const;
    ServerEntry defaultServer() const;
    QString displayName(const QString &url) const;

    bool setDefault(const QString &url);
    bool add(const QString &name, const QString &address, QString *error = nullptr);
    bool update(const QString &url, const QString &name, const QString &address, QString *error = nullptr);
    bool remove(const QString &url);
    void remember(const QString &url);

private:
    ServerEntry m_builtIn;
    QList<ServerEntry> m_custom;
    QString m_default;
    bool m_defaultFromEnvironment = false;
};

}
