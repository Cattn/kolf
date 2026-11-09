// SPDX-License-Identifier: GPL-2.0-or-later
#include "effects.h"

#include <KConfigGroup>
#include <KSharedConfig>

namespace Kolf::Effects {
namespace {

KConfigGroup settings()
{
    return KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Settings"));
}

}

Level level()
{
    const QString value = settings().readEntry("effects", QStringLiteral("full"));
    if (value == QLatin1String("reduced")) return Level::Reduced;
    if (value == QLatin1String("off")) return Level::Off;
    return Level::Full;
}

void setLevel(Level level)
{
    auto group = settings();
    group.writeEntry("effects", level == Level::Off ? QStringLiteral("off")
        : level == Level::Reduced ? QStringLiteral("reduced") : QStringLiteral("full"));
    group.sync();
    Q_EMIT notifier()->changed();
}

bool ballTrails()
{
    return settings().readEntry("ballTrails", false);
}

void setBallTrails(bool enabled)
{
    auto group = settings();
    group.writeEntry("ballTrails", enabled);
    group.sync();
    Q_EMIT notifier()->changed();
}

int volume()
{
    return qBound(0, settings().readEntry("volume", 80), 100);
}

void setVolume(int percent)
{
    auto group = settings();
    group.writeEntry("volume", qBound(0, percent, 100));
    group.sync();
    Q_EMIT notifier()->changed();
}

bool plainMode()
{
    return settings().readEntry("plainMode", false);
}

void setPlainMode(bool plain)
{
    auto group = settings();
    group.writeEntry("plainMode", plain);
    group.sync();
    Q_EMIT notifier()->changed();
}

Notifier *notifier()
{
    static Notifier instance;
    return &instance;
}

}
