// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QWidget>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class KColorButton;

namespace Kolf::Online {
class PlayerProfileEditor : public QWidget {
public:
    explicit PlayerProfileEditor(QWidget *parent = nullptr);
    void setProfile(const QString &name, const QString &mode, const QString &customColor);
    QString playerName() const;
    QString colorMode() const;
    QString customColor() const;
    bool isValid() const;

private:
    void updatePreview();
    QLineEdit *m_name;
    QComboBox *m_choice;
    KColorButton *m_custom;
    QLabel *m_preview;
};
}
