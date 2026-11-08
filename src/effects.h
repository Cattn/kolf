// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>

namespace Kolf::Effects {

enum class Level { Full, Reduced, Off };

Level level();
void setLevel(Level level);
bool ballTrails();
void setBallTrails(bool enabled);

class Notifier : public QObject {
    Q_OBJECT
Q_SIGNALS:
    void changed();
};
Notifier *notifier();

}
