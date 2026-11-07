// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

#include <optional>

namespace Kolf::Online {

struct Invite {
    QString code;
    QString serverUrl;
};

QString normalizeInviteCode(const QString &text);
std::optional<Invite> parseInviteLink(const QString &text);
QString inviteLinkFor(const QString &serverUrl, const QString &code);

}
