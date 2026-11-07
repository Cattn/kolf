// SPDX-License-Identifier: GPL-2.0-or-later
#include "invitelink.h"

#include "serverlist.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace Kolf::Online {

QString normalizeInviteCode(const QString &text)
{
    QString code;
    for (const QChar character : text)
        if (character.isLetterOrNumber()) code.append(character.toUpper());
    static const QRegularExpression valid(QStringLiteral("^[A-HJ-NP-Z2-9]{6,12}$"));
    return valid.match(code).hasMatch() ? code : QString();
}

std::optional<Invite> parseInviteLink(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid()) return std::nullopt;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("kolf")) {
        const QString location = url.host() + url.path();
        const QStringList parts = location.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() != 2 || parts[0].toLower() != QLatin1String("join")) return std::nullopt;
        const QString code = normalizeInviteCode(parts[1]);
        if (code.isEmpty()) return std::nullopt;
        const QString server = QUrlQuery(url).queryItemValue(QStringLiteral("server"), QUrl::FullyDecoded);
        if (server.isEmpty()) return Invite{code, QString()};
        const auto address = normalizeServerAddress(server);
        if (!address.isValid()) return std::nullopt;
        return Invite{code, address.url.toString()};
    }
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http")) return std::nullopt;
    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 2 || parts[0] != QLatin1String("join") || url.host().isEmpty()) return std::nullopt;
    const QString code = normalizeInviteCode(parts[1]);
    if (code.isEmpty()) return std::nullopt;
    QUrl server;
    server.setScheme(scheme == QLatin1String("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
    server.setHost(url.host());
    if (url.port() > 0) server.setPort(url.port());
    server.setPath(scheme == QLatin1String("https") ? QStringLiteral("/ws") : QString());
    const auto address = normalizeServerAddress(server.toString());
    if (!address.isValid()) return std::nullopt;
    return Invite{code, address.url.toString()};
}

QString inviteLinkFor(const QString &serverUrl, const QString &code)
{
    const auto address = normalizeServerAddress(serverUrl);
    const QString normalized = normalizeInviteCode(code);
    if (!address.isValid() || normalized.isEmpty()) return {};
    QUrl link;
    link.setScheme(address.url.scheme() == QLatin1String("wss") ? QStringLiteral("https") : QStringLiteral("http"));
    link.setHost(address.url.host());
    if (address.url.port() > 0) link.setPort(address.url.port());
    link.setPath(QStringLiteral("/join/") + normalized);
    return link.toString();
}

}
