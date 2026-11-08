// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>

namespace Kolf::Effects {

enum class Level { Full, Reduced, Off };

Level level();
void setLevel(Level level);
bool ballTrails();
void setBallTrails(bool enabled);
int volume();
void setVolume(int percent);

class Notifier : public QObject {
    Q_OBJECT
Q_SIGNALS:
    void changed();
};
Notifier *notifier();

}
