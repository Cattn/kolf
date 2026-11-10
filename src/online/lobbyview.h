#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Kolf::Online {
class OnlineCoordinator;
class LobbyView : public QWidget {
    Q_OBJECT
public:
    LobbyView(OnlineCoordinator &coordinator, QWidget *parent = nullptr);
    void setState(const QJsonObject &state);
    void setStatus(const QString &status);

Q_SIGNALS:
    void leaveRequested();

private:
    void editPlayer(const QJsonObject &player = {});
    void updateThumbnail(const QJsonObject &course);
    OnlineCoordinator &m_coordinator;
    QJsonObject m_state;
    QLabel *m_heading;
    QLabel *m_peopleHeading;
    QVBoxLayout *m_people;
    QComboBox *m_course;
    QLabel *m_thumbnail;
    QLabel *m_courseDetails;
    QLabel *m_courseStatus;
    QLabel *m_status;
    QPushButton *m_custom;
    QPushButton *m_primary;
    QCheckBox *m_hostControls;
    QCheckBox *m_allowVoting;
    QComboBox *m_powerUps;
    QString m_thumbnailKey;
};
}
