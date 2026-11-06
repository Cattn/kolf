// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "results/summary.h"

#include <QScrollArea>

class QLabel;
class QTableWidget;

namespace Kolf::Ui {
class ResultsView : public QScrollArea {
    Q_OBJECT
public:
    explicit ResultsView(QWidget *parent = nullptr);
    void setSummary(const Results::Summary &summary);
    QString plainText() const;

private:
    QLabel *m_headline;
    QLabel *m_subtitle;
    QLabel *m_status;
    QTableWidget *m_standings;
    QTableWidget *m_scorecard;
    QLabel *m_highlights;
};
}
