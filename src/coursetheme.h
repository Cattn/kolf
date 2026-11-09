// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QString>

class KConfigGroup;

namespace Kolf {

struct CourseTheme {
    QString name;
    QColor ground;
    QColor border;
    QColor wall;
    bool stars = false;

    bool isDefault() const { return !ground.isValid() && !border.isValid() && !wall.isValid() && !stars; }
    static CourseTheme preset(const QString &name);
    static CourseTheme read(const KConfigGroup &course, const KConfigGroup &hole);
    static QStringList presetNames();
};

class ThemeGround : public QGraphicsItem {
public:
    explicit ThemeGround(QGraphicsItem *parent);
    void setTheme(const CourseTheme &theme);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    CourseTheme m_theme;
};

}
