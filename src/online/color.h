// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QString>

namespace Kolf::Online {
// The wire format is #RRGGBBAA. QColor's eight-digit parser uses #AARRGGBB.
inline QColor colorFromRgba(const QString &rgba)
{
    if (rgba.size() != 9 || !rgba.startsWith(QLatin1Char('#'))) return {};
    bool ok = false;
    const auto value = rgba.mid(1).toUInt(&ok, 16);
    if (!ok) return {};
    return QColor::fromRgba(qRgba((value >> 24) & 0xff, (value >> 16) & 0xff,
                                   (value >> 8) & 0xff, value & 0xff));
}

inline QString rgbaFromColor(const QColor &color)
{
    return QStringLiteral("#%1%2%3%4")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.alpha(), 2, 16, QLatin1Char('0'));
}
}
