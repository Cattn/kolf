// SPDX-License-Identifier: GPL-2.0-or-later
#include "coursetheme.h"

#include "effects.h"
#include "objects.h"

#include <KConfigGroup>
#include <QPainter>
#include <QRandomGenerator>

namespace Kolf {

QStringList CourseTheme::presetNames()
{
    return {QStringLiteral("classic"), QStringLiteral("beach"), QStringLiteral("snow"), QStringLiteral("desert"),
        QStringLiteral("autumn"), QStringLiteral("night"), QStringLiteral("neon"), QStringLiteral("space")};
}

CourseTheme CourseTheme::preset(const QString &name)
{
    CourseTheme theme;
    theme.name = name;
    if (name == QLatin1String("beach")) {
        theme.ground = QColor(0xe9, 0xd5, 0x9b); theme.border = QColor(0x2e, 0x86, 0xc1); theme.wall = QColor(0xa0, 0x6a, 0x3c);
    } else if (name == QLatin1String("snow")) {
        theme.ground = QColor(0xe8, 0xf1, 0xf8); theme.border = QColor(0x34, 0x49, 0x5e); theme.wall = QColor(0x5d, 0x6d, 0x7e);
    } else if (name == QLatin1String("desert")) {
        theme.ground = QColor(0xd9, 0xb3, 0x6c); theme.border = QColor(0x7e, 0x46, 0x1c); theme.wall = QColor(0x9a, 0x5b, 0x2b);
    } else if (name == QLatin1String("autumn")) {
        theme.ground = QColor(0x8f, 0x9b, 0x4a); theme.border = QColor(0x8b, 0x2e, 0x16); theme.wall = QColor(0xb9, 0x57, 0x1d);
    } else if (name == QLatin1String("night")) {
        theme.ground = QColor(0x1f, 0x3d, 0x2b); theme.border = QColor(0xf4, 0xd0, 0x3f); theme.wall = QColor(0xaa, 0xb7, 0xb8);
    } else if (name == QLatin1String("neon")) {
        theme.ground = QColor(0x10, 0x0c, 0x24); theme.border = QColor(0xff, 0x2b, 0xd6); theme.wall = QColor(0x00, 0xe5, 0xff);
    } else if (name == QLatin1String("space")) {
        theme.ground = QColor(0x07, 0x09, 0x1a); theme.border = QColor(0x8e, 0x9a, 0xff); theme.wall = QColor(0xc3, 0xcf, 0xe2);
        theme.stars = true;
    } else {
        theme.name = QStringLiteral("classic");
    }
    return theme;
}

CourseTheme CourseTheme::read(const KConfigGroup &course, const KConfigGroup &hole)
{
    const auto value = [&](const char *key) {
        return hole.hasKey(key) ? hole.readEntry(key, QString()) : course.readEntry(key, QString());
    };
    CourseTheme theme = preset(value("theme").trimmed().toLower());
    for (const auto &[key, target] : {std::pair<const char *, QColor *>{"groundColor", &theme.ground},
             {"borderColor", &theme.border}, {"wallColor", &theme.wall}}) {
        const QColor color = QColor::fromString(value(key).trimmed());
        if (color.isValid())
            *target = color;
    }
    return theme;
}

ThemeGround::ThemeGround(QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
    setZValue(-100000);
    setAcceptedMouseButtons(Qt::NoButton);
    setVisible(false);
}

void ThemeGround::setTheme(const CourseTheme &theme)
{
    m_theme = theme;
    setVisible(theme.ground.isValid());
    update();
}

QRectF ThemeGround::boundingRect() const
{
    return QRectF(-2000, -2000, 4400, 4400);
}

void ThemeGround::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!m_theme.ground.isValid())
        return;
    painter->fillRect(boundingRect(), m_theme.ground.darker(115));
    QLinearGradient gradient(0, 0, 0, 400);
    gradient.setColorAt(0, m_theme.ground.lighter(104));
    gradient.setColorAt(1, m_theme.ground.darker(104));
    painter->fillRect(QRectF(0, 0, 400, 400), gradient);
    if (m_theme.ground.lightnessF() < 0.35 && parentItem()) {
        for (QGraphicsItem *item : parentItem()->childItems()) {
            if (!dynamic_cast<Kolf::Cup *>(item) || !item->isVisible())
                continue;
            QRadialGradient halo(item->pos(), 16);
            halo.setColorAt(0, QColor(255, 255, 255, 200));
            halo.setColorAt(0.55, QColor(255, 255, 255, 90));
            halo.setColorAt(1, QColor(255, 255, 255, 0));
            painter->setPen(Qt::NoPen);
            painter->setBrush(halo);
            painter->drawEllipse(item->pos(), 16, 16);
        }
    }
    if (!m_theme.stars || Effects::plainMode())
        return;
    QRandomGenerator stars(0x4b6f6c66u);
    painter->setPen(Qt::NoPen);
    for (int i = 0; i < 220; ++i) {
        const QPointF point(-200.0 + stars.bounded(800.0), -200.0 + stars.bounded(800.0));
        const double size = 0.4 + stars.bounded(1.2);
        painter->setBrush(QColor(255, 255, 255, 80 + stars.bounded(170)));
        painter->drawEllipse(point, size, size);
    }
}

}
