// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
namespace Kolf { class ItemFactory; }

namespace Kolf::Ui {
struct LocalPlayer {
    QString name;
    QColor color;
};

class NewGameView : public QWidget {
    Q_OBJECT
public:
    NewGameView(const Kolf::ItemFactory &factory, QWidget *parent = nullptr);
    void prepare(const QString &lockedCourse = {});
    static QPixmap thumbnail(const Kolf::ItemFactory &factory, const QString &course);

Q_SIGNALS:
    void startRequested(const QList<Kolf::Ui::LocalPlayer> &players, const QString &course, bool strict);
    void backRequested();
    void highScoresRequested(const QString &course);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void loadCourses();
    void addPlayerRow(const QString &name, const QColor &color);
    void renumberPlayers();
    void updateCourse();
    void step(int delta);
    void openCourseFile();
    void removeCourse();
    void start();
    QColor nextFreeColor() const;

    const Kolf::ItemFactory &m_factory;
    QVBoxLayout *m_playerRows;
    QPushButton *m_addPlayer;
    QComboBox *m_course;
    QToolButton *m_previous;
    QToolButton *m_next;
    QLabel *m_thumbnail;
    QLabel *m_courseName;
    QLabel *m_courseDetails;
    QPushButton *m_openCourse;
    QPushButton *m_removeCourse;
    QPushButton *m_highScores;
    QCheckBox *m_strict;
    QPushButton *m_start;
    QStringList m_extraCourses;
    bool m_locked = false;
};
}

Q_DECLARE_METATYPE(QList<Kolf::Ui::LocalPlayer>)
