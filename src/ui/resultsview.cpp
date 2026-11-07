// SPDX-License-Identifier: GPL-2.0-or-later
#include "resultsview.h"

#include <KLocalizedString>

#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace Kolf::Ui;
using Kolf::Results::HoleHighlight;
using Kolf::Results::Summary;

namespace {
QIcon swatch(const QColor &color)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0, 0, 0, 150), 1));
    painter.setBrush(color.isValid() ? color : QColor(Qt::white));
    painter.drawEllipse(QRectF(1, 1, 12, 12));
    return QIcon(pixmap);
}

QString relativeText(const std::optional<int> &relative)
{
    if (!relative) return i18nc("no value", "—");
    if (*relative == 0) return i18nc("score equal to par", "E");
    return *relative > 0 ? QStringLiteral("+%1").arg(*relative) : QStringLiteral("−%1").arg(-*relative);
}

QString holeText(const HoleHighlight &hole)
{
    if (hole.strokes == 1) return i18n("hole %1 (hole in one)", hole.hole);
    if (hole.relativeToPar) return i18n("hole %1 (%2, %3)", hole.hole, hole.strokes, relativeText(hole.relativeToPar));
    return i18n("hole %1 (%2)", hole.hole, hole.strokes);
}

QTableWidget *table(QWidget *parent, const QString &name)
{
    auto *widget = new QTableWidget(parent);
    widget->setObjectName(name);
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->setSelectionMode(QAbstractItemView::NoSelection);
    widget->setFocusPolicy(Qt::TabFocus);
    widget->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    widget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widget->verticalHeader()->setVisible(false);
    return widget;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

void fitHeight(QTableWidget *widget)
{
    widget->resizeColumnsToContents();
    int height = widget->horizontalHeader()->height() + 2 * widget->frameWidth();
    for (int row = 0; row < widget->rowCount(); ++row) height += widget->rowHeight(row);
    widget->setFixedHeight(height);
}
}

ResultsView::ResultsView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("resultsView"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(8);

    m_headline = new QLabel(content);
    m_headline->setObjectName(QStringLiteral("resultsHeadline"));
    QFont headlineFont = m_headline->font();
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 2.0);
    headlineFont.setBold(true);
    m_headline->setFont(headlineFont);
    m_headline->setAlignment(Qt::AlignCenter);
    m_headline->setWordWrap(true);
    layout->addWidget(m_headline);
    m_subtitle = new QLabel(content);
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_subtitle);
    m_status = new QLabel(content);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    layout->addWidget(sectionTitle(i18n("Standings"), content));
    m_standings = table(content, QStringLiteral("resultsStandings"));
    layout->addWidget(m_standings);
    layout->addWidget(sectionTitle(i18n("Scorecard"), content));
    m_scorecard = table(content, QStringLiteral("resultsScorecard"));
    m_scorecard->verticalHeader()->setVisible(true);
    m_scorecard->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(m_scorecard);
    auto *legend = new QLabel(i18n("★ hole in one · ▼ under par · ▲ over par · — not played"), content);
    legend->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(legend);
    layout->addWidget(sectionTitle(i18n("Highlights"), content));
    m_highlights = new QLabel(content);
    m_highlights->setWordWrap(true);
    m_highlights->setTextFormat(Qt::PlainText);
    layout->addWidget(m_highlights);
    layout->addStretch();
    setWidget(content);
}

void ResultsView::setSummary(const Summary &summary)
{
    QStringList winnerNames;
    for (int winner : summary.winners) winnerNames.append(summary.players.value(winner).name);
    const bool partial = summary.hasPartialHoles();
    if (!summary.completed) {
        m_headline->setText(i18n("Match interrupted"));
        m_status->setText(summary.interruptionReason.isEmpty() ? i18n("Match interrupted — partial scores")
            : i18n("Match interrupted — partial scores. %1", summary.interruptionReason));
    } else {
        if (winnerNames.isEmpty()) m_headline->setText(i18n("Game over"));
        else if (winnerNames.size() == 1)
            m_headline->setText(partial ? i18n("Lowest recorded strokes: %1", winnerNames.first())
                : i18nc("@title results headline", "%1 wins!", winnerNames.first()));
        else
            m_headline->setText(partial ? i18n("Lowest recorded strokes (tie): %1", winnerNames.join(i18nc("list separator", ", ")))
                : i18nc("@title results headline", "%1 tie!", winnerNames.join(i18nc("@item:intext winners", " and "))));
        m_status->setText(partial ? i18n("Match complete — unfinished holes contain partial scores") : QString());
    }
    m_status->setVisible(!m_status->text().isEmpty());
    QString subtitle = summary.courseName;
    if (!summary.winners.isEmpty()) {
        for (const auto &standing : summary.standings) {
            if (standing.player != summary.winners.first()) continue;
            subtitle += i18nc("results subtitle: strokes", " · %1 strokes", standing.total);
            if (standing.relativeToPar) subtitle += QStringLiteral(" · ") + relativeText(standing.relativeToPar);
        }
    }
    if (summary.durationMs >= 0)
        subtitle += i18nc("results subtitle: minutes", " · %1 min", qMax<qint64>(1, (summary.durationMs + 30000) / 60000));
    m_subtitle->setText(subtitle);

    const bool online = std::any_of(summary.players.cbegin(), summary.players.cend(),
        [](const auto &player) { return player.acceptedShots >= 0; });
    QStringList standingHeaders{i18n("Rank"), i18n("Player"), i18n("Holes"), i18n("Strokes"), i18n("vs Par"), i18n("Aces")};
    if (online) standingHeaders << i18n("Shots / hazard choices");
    m_standings->clear();
    m_standings->setColumnCount(int(standingHeaders.size()));
    m_standings->setHorizontalHeaderLabels(standingHeaders);
    m_standings->setRowCount(int(summary.standings.size()));
    for (int row = 0; row < summary.standings.size(); ++row) {
        const auto &standing = summary.standings.at(row);
        const auto &player = summary.players.value(standing.player);
        auto *name = new QTableWidgetItem(swatch(player.color), player.name);
        const QString place = standing.leftOnHole ? i18nc("@item results place", "Left on hole %1", *standing.leftOnHole)
                                                  : QString::number(standing.rank);
        QStringList values{place, QString(), QString::number(standing.completedHoles),
            QString::number(standing.total), relativeText(standing.relativeToPar), QString::number(standing.holesInOne)};
        if (online) values << QStringLiteral("%1 / %2").arg(player.acceptedShots).arg(player.hazardChoices);
        for (int column = 0; column < values.size(); ++column) {
            auto *item = column == 1 ? name : new QTableWidgetItem(values.at(column));
            if (column != 1) item->setTextAlignment(Qt::AlignCenter);
            if (standing.rank == 1 && summary.completed) { QFont font = item->font(); font.setBold(true); item->setFont(font); }
            m_standings->setItem(row, column, item);
        }
    }
    m_standings->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fitHeight(m_standings);

    const int holes = summary.holeCount();
    QStringList holeHeaders;
    for (int hole = 1; hole <= holes; ++hole) {
        holeHeaders << (summary.skippedHoles.contains(hole) ? i18n("%1 (skipped)", hole)
            : summary.navigatedHoles.contains(hole) ? i18n("%1 (left unfinished)", hole) : QString::number(hole));
    }
    holeHeaders << i18n("Total");
    m_scorecard->clear();
    m_scorecard->setColumnCount(int(holeHeaders.size()));
    m_scorecard->setHorizontalHeaderLabels(holeHeaders);
    m_scorecard->setRowCount(int(summary.players.size()) + 1);
    QStringList rowHeaders{i18n("Par")};
    int parTotal = 0;
    for (int hole = 0; hole < holes; ++hole) {
        const int par = summary.par.value(hole);
        parTotal += qMax(0, par);
        auto *item = new QTableWidgetItem(par > 0 ? QString::number(par) : i18nc("no value", "—"));
        item->setTextAlignment(Qt::AlignCenter);
        item->setForeground(palette().brush(QPalette::PlaceholderText));
        m_scorecard->setItem(0, hole, item);
    }
    auto *parTotalItem = new QTableWidgetItem(QString::number(parTotal));
    parTotalItem->setTextAlignment(Qt::AlignCenter);
    m_scorecard->setItem(0, holes, parTotalItem);
    const QColor ace(212, 160, 23), under(46, 139, 87), over(205, 92, 70);
    for (int index = 0; index < summary.players.size(); ++index) {
        const auto &player = summary.players.at(index);
        rowHeaders << player.name;
        int total = 0;
        for (int hole = 0; hole < holes; ++hole) {
            const int strokes = player.scores.value(hole);
            const int par = summary.par.value(hole);
            total += qMax(0, strokes);
            QString text = strokes > 0 ? QString::number(strokes) : i18nc("not played", "—");
            QColor tint;
            if (strokes == 1) { text += QStringLiteral(" ★"); tint = ace; }
            else if (strokes > 0 && par > 0 && strokes < par) { text += QStringLiteral(" ▼"); tint = under; }
            else if (strokes > 0 && par > 0 && strokes > par) { text += QStringLiteral(" ▲"); tint = over; }
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            if (tint.isValid()) {
                QColor background = tint;
                background.setAlpha(70);
                item->setBackground(background);
            }
            m_scorecard->setItem(index + 1, hole, item);
        }
        auto *totalItem = new QTableWidgetItem(QString::number(total));
        totalItem->setTextAlignment(Qt::AlignCenter);
        QFont font = totalItem->font();
        font.setBold(true);
        totalItem->setFont(font);
        m_scorecard->setItem(index + 1, holes, totalItem);
    }
    m_scorecard->setVerticalHeaderLabels(rowHeaders);
    fitHeight(m_scorecard);

    QStringList highlights;
    for (const auto &standing : summary.standings) {
        const auto &player = summary.players.value(standing.player);
        if (!standing.bestHole) {
            highlights << i18n("%1: no completed holes", player.name);
            continue;
        }
        QString line = i18n("%1: best %2", player.name, holeText(*standing.bestHole));
        if (standing.worstHole && standing.worstHole->hole != standing.bestHole->hole)
            line += i18n("; toughest %1", holeText(*standing.worstHole));
        if (standing.holesInOne > 1) line += i18n("; %1 holes in one", standing.holesInOne);
        highlights << line;
    }
    for (int hole : summary.skippedHoles) highlights << i18n("Hole %1 was skipped; its partial strokes still count.", hole);
    m_highlights->setText(highlights.join(QLatin1Char('\n')));
}

QString ResultsView::plainText() const
{
    QStringList lines{m_headline->text(), m_subtitle->text()};
    if (m_status->isVisibleTo(this)) lines << m_status->text();
    QStringList headers;
    for (int column = 0; column < m_scorecard->columnCount(); ++column)
        if (auto *item = m_scorecard->horizontalHeaderItem(column)) headers << item->text();
    lines << headers.join(QLatin1Char('\t'));
    for (int row = 0; row < m_standings->rowCount(); ++row) {
        QStringList cells;
        for (int column = 0; column < m_standings->columnCount(); ++column)
            if (auto *item = m_standings->item(row, column)) cells << item->text();
        lines << cells.join(QLatin1Char('\t'));
    }
    lines << m_highlights->text();
    return lines.join(QLatin1Char('\n'));
}
