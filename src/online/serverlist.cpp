// SPDX-License-Identifier: GPL-2.0-or-later
#include "serverlist.h"

#include <KConfigGroup>
#include <KLocalizedString>

#include <QHostAddress>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#ifndef KOLF_DEFAULT_SERVER_URL
#define KOLF_DEFAULT_SERVER_URL "wss://kolf.pro/ws"
#endif
#ifndef KOLF_DEFAULT_SERVER_NAME
#define KOLF_DEFAULT_SERVER_NAME "kolf.pro"
#endif

namespace Kolf::Online {

namespace {
constexpr int defaultLocalPort = 3011;
constexpr int maximumCustomServers = 12;

QString hostPart(const QString &authority)
{
    if (authority.startsWith(QLatin1Char('['))) {
        const int end = authority.indexOf(QLatin1Char(']'));
        return end > 0 ? authority.mid(1, end - 1) : QString();
    }
    if (authority.count(QLatin1Char(':')) > 1) return authority;
    const int colon = authority.indexOf(QLatin1Char(':'));
    return colon >= 0 ? authority.left(colon) : authority;
}
}

bool isPrivateHost(const QString &host)
{
    const QString lower = host.toLower();
    if (lower == QLatin1String("localhost") || lower.endsWith(QLatin1String(".localhost"))
        || lower.endsWith(QLatin1String(".local")) || lower.endsWith(QLatin1String(".lan"))
        || lower.endsWith(QLatin1String(".home.arpa")))
        return true;
    QHostAddress address;
    if (!address.setAddress(lower)) return false;
    if (address.isLoopback() || address.isLinkLocal() || address.isUniqueLocalUnicast()) return true;
    bool ok = false;
    const quint32 ipv4 = address.toIPv4Address(&ok);
    if (!ok) return false;
    return (ipv4 >> 24) == 10 || (ipv4 >> 20) == ((172u << 4) | 1u) || (ipv4 >> 16) == ((192u << 8) | 168u);
}

ServerAddress normalizeServerAddress(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty()) return {{}, i18n("Enter a server address.")};
    const int schemeEnd = text.indexOf(QStringLiteral("://"));
    if (schemeEnd < 0) {
        const QString authority = text.section(QLatin1Char('/'), 0, 0);
        const QString host = hostPart(authority);
        if (isPrivateHost(host)) {
            const bool hasPort = authority.startsWith(QLatin1Char('['))
                ? authority.contains(QStringLiteral("]:")) : authority.count(QLatin1Char(':')) == 1;
            QString bracketed = authority;
            if (!authority.startsWith(QLatin1Char('[')) && authority.count(QLatin1Char(':')) > 1)
                bracketed = QLatin1Char('[') + authority + QLatin1Char(']');
            text = QStringLiteral("ws://") + bracketed + (hasPort ? QString() : QStringLiteral(":%1").arg(defaultLocalPort))
                + text.mid(authority.size());
        } else {
            text.prepend(QStringLiteral("wss://"));
        }
    } else {
        const QString scheme = text.left(schemeEnd).toLower();
        if (scheme == QLatin1String("https")) text = QStringLiteral("wss") + text.mid(schemeEnd);
        else if (scheme == QLatin1String("http")) text = QStringLiteral("ws") + text.mid(schemeEnd);
    }
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return {{}, i18n("“%1” is not a valid server address.", input.trimmed())};
    if (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss"))
        return {{}, i18n("Server addresses start with wss:// (or ws:// on a local network).")};
    if (!url.userName().isEmpty() || !url.password().isEmpty() || url.hasQuery() || url.hasFragment())
        return {{}, i18n("Server addresses can't contain a user name, password, or query.")};
    const bool local = isPrivateHost(url.host());
    if (url.scheme() == QLatin1String("ws") && !local)
        return {{}, i18n("Servers on the internet need an encrypted wss:// address.")};
    if (url.path().isEmpty() || url.path() == QLatin1String("/"))
        url.setPath(local ? QString() : QStringLiteral("/ws"));
    url.setHost(url.host().toLower());
    return {url, {}};
}

bool isNewerClientVersion(const QString &candidate, const QString &current)
{
    const auto numbers = [](const QString &version) {
        QList<int> parts;
        static const QRegularExpression digits(QStringLiteral("[0-9]+"));
        auto it = digits.globalMatch(version);
        while (it.hasNext()) parts.append(it.next().captured().toInt());
        return parts;
    };
    const auto a = numbers(candidate), b = numbers(current);
    if (a.isEmpty()) return false;
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int left = a.value(i, 0), right = b.value(i, 0);
        if (left != right) return left > right;
    }
    return false;
}

ServerEntry ServerList::builtInServer()
{
    const QString overrideAddress = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed();
    if (!overrideAddress.isEmpty()) {
        const auto normalized = normalizeServerAddress(overrideAddress);
        if (normalized.isValid()) {
            QString name = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_SERVER_NAME").trimmed();
            if (name.isEmpty()) name = normalized.url.host();
            return {name, normalized.url.toString(), true};
        }
    }
    const auto normalized = normalizeServerAddress(QStringLiteral(KOLF_DEFAULT_SERVER_URL));
    return {QStringLiteral(KOLF_DEFAULT_SERVER_NAME), normalized.url.toString(), true};
}

ServerList ServerList::load(KConfigGroup &group)
{
    ServerList list;
    list.m_builtIn = builtInServer();
    list.m_defaultFromEnvironment = !qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed().isEmpty();
    if (group.hasKey("servers")) {
        const auto array = QJsonDocument::fromJson(group.readEntry("servers", QString()).toUtf8()).array();
        for (const auto &value : array) {
            const auto object = value.toObject();
            const auto normalized = normalizeServerAddress(object.value(QStringLiteral("url")).toString());
            if (!normalized.isValid()) continue;
            const QString url = normalized.url.toString();
            if (list.indexOf(url) >= 0) continue;
            const QString name = object.value(QStringLiteral("name")).toString().trimmed();
            list.m_custom.append({name.isEmpty() ? normalized.url.host() : name, url, false});
        }
        list.m_default = group.readEntry("defaultServer", QString());
    } else {
        QStringList legacy;
        const QString lastSuccessful = group.readEntry("lastSuccessfulEndpoint", QString()).trimmed();
        legacy << lastSuccessful << group.readEntry("endpoint", QString()).trimmed()
               << group.readEntry("recentServers", QStringList());
        for (const auto &address : std::as_const(legacy)) list.remember(address);
        const auto normalizedLast = normalizeServerAddress(lastSuccessful);
        if (normalizedLast.isValid()) list.m_default = normalizedLast.url.toString();
        list.save(group);
    }
    if (list.indexOf(list.m_default) < 0) list.m_default = list.m_builtIn.url;
    return list;
}

void ServerList::save(KConfigGroup &group) const
{
    QJsonArray array;
    for (const auto &entry : m_custom)
        array.append(QJsonObject{{QStringLiteral("name"), entry.name}, {QStringLiteral("url"), entry.url}});
    group.writeEntry("servers", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    if (m_default.isEmpty() || m_default == m_builtIn.url) group.deleteEntry("defaultServer");
    else group.writeEntry("defaultServer", m_default);
}

QList<ServerEntry> ServerList::servers() const
{
    QList<ServerEntry> result{m_builtIn};
    result.append(m_custom);
    return result;
}

int ServerList::indexOf(const QString &url) const
{
    if (url.isEmpty()) return -1;
    if (url == m_builtIn.url) return 0;
    for (int i = 0; i < m_custom.size(); ++i)
        if (m_custom[i].url == url) return i + 1;
    return -1;
}

QString ServerList::defaultUrl() const
{
    if (m_defaultFromEnvironment) return m_builtIn.url;
    return indexOf(m_default) >= 0 ? m_default : m_builtIn.url;
}

ServerEntry ServerList::defaultServer() const
{
    return servers().value(indexOf(defaultUrl()));
}

QString ServerList::displayName(const QString &url) const
{
    const int index = indexOf(url);
    if (index >= 0) return servers().at(index).name;
    return QUrl(url).host().isEmpty() ? url : QUrl(url).host();
}

bool ServerList::setDefault(const QString &url)
{
    if (indexOf(url) < 0) return false;
    m_default = url;
    return true;
}

bool ServerList::add(const QString &name, const QString &address, QString *error)
{
    const auto normalized = normalizeServerAddress(address);
    if (!normalized.isValid()) {
        if (error) *error = normalized.error;
        return false;
    }
    const QString url = normalized.url.toString();
    if (indexOf(url) >= 0) {
        if (error) *error = i18n("That server is already in the list.");
        return false;
    }
    const QString trimmed = name.trimmed();
    m_custom.append({trimmed.isEmpty() ? normalized.url.host() : trimmed, url, false});
    return true;
}

bool ServerList::update(const QString &url, const QString &name, const QString &address, QString *error)
{
    const int index = indexOf(url);
    if (index <= 0) return false;
    const auto normalized = normalizeServerAddress(address);
    if (!normalized.isValid()) {
        if (error) *error = normalized.error;
        return false;
    }
    const QString newUrl = normalized.url.toString();
    const int existing = indexOf(newUrl);
    if (existing >= 0 && existing != index) {
        if (error) *error = i18n("That server is already in the list.");
        return false;
    }
    const QString trimmed = name.trimmed();
    m_custom[index - 1] = {trimmed.isEmpty() ? normalized.url.host() : trimmed, newUrl, false};
    if (m_default == url) m_default = newUrl;
    return true;
}

bool ServerList::remove(const QString &url)
{
    const int index = indexOf(url);
    if (index <= 0) return false;
    m_custom.removeAt(index - 1);
    if (m_default == url) m_default = m_builtIn.url;
    return true;
}

void ServerList::remember(const QString &address)
{
    const auto normalized = normalizeServerAddress(address);
    if (!normalized.isValid()) return;
    const QString url = normalized.url.toString();
    if (indexOf(url) >= 0) return;
    if (m_custom.size() >= maximumCustomServers) return;
    m_custom.append({normalized.url.host(), url, false});
}

}
