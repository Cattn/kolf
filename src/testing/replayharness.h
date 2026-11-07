// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>

namespace Kolf::Testing {
QJsonObject replayCourse(const QString &courseFile, quint32 seed, QString &error);
int runReplayFromEnvironment();
}
