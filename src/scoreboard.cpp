/*
    Copyright (C) 2002-2005, Jason Katz-Brown <jasonkb@mit.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "scoreboard.h"
#include "online/color.h"

#include <QHeaderView>
#include <QJsonObject>
#include <KLocalizedString>

ScoreBoard::ScoreBoard(QWidget *parent)
	: QTableWidget(1, 1, parent) 
{
	setVerticalHeaderItem(rowCount() -1,  new QTableWidgetItem(i18nc("@title:row", "Par")));
	setHorizontalHeaderItem(columnCount() -1, new QTableWidgetItem(i18nc("@title:column", "Total")));

	setFocusPolicy(Qt::NoFocus);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	
	resizeColumnToContents(columnCount() - 1);
	
	verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	
	doUpdateHeight();
}

void ScoreBoard::newHole(int par)
{
	int _columnCount = columnCount();
	insertColumn(_columnCount - 1);
	setHorizontalHeaderItem(columnCount() -2, new QTableWidgetItem(QString::number(columnCount() - 1)));
	//set each player's score to 0
	for (int i = 0; i < rowCount() - 1; i++)
		setItem(i, columnCount() -2, new QTableWidgetItem(QString::number(0)));
	setItem(rowCount() - 1, columnCount() - 2, new QTableWidgetItem(QString::number(par)));

	// update total
	int tot = 0;
	for (int i = 0; i < columnCount() - 1; ++i)
		tot += item(rowCount() - 1, i)->text().toInt();
	setItem(rowCount() - 1, columnCount() - 1, new QTableWidgetItem(QString::number(tot)));
	
	resizeColumnToContents(columnCount() - 2); 
}

void ScoreBoard::newPlayer(const QString &name)
{
	//kDebug(12007) << "name of new player is" << name;
	insertRow(rowCount() - 1);
	setVerticalHeaderItem(rowCount() -2, new QTableWidgetItem(name));

	doUpdateHeight();
}

void ScoreBoard::setScore(int id, int hole, int score)
{
	setItem(id - 1, hole - 1, new QTableWidgetItem(QString::number(score)));

	QString name;
	setItem(id - 1, columnCount() - 1, new QTableWidgetItem(QString::number(total(id, name))));
	
	resizeColumnToContents(hole -1);
		
	setCurrentCell(id - 1, hole - 1);
}

void ScoreBoard::parChanged(int hole, int par)
{
	setItem(rowCount() - 1, hole - 1, new QTableWidgetItem(QString::number(par)));

	// update total
	int tot = 0;
	for (int i = 0; i < columnCount() - 1; ++i)
		tot += item(rowCount() - 1, i)->text().toInt();
	setItem(rowCount() - 1, columnCount() - 1, new QTableWidgetItem(QString::number(tot)));
}

void ScoreBoard::resetPlayers(const QStringList &names)
{
	clear();
	setRowCount(names.size() + 1);
	setColumnCount(1);
	setHorizontalHeaderItem(0, new QTableWidgetItem(i18nc("@title:column", "Total")));
	for (int row = 0; row < names.size(); ++row) {
		setVerticalHeaderItem(row, new QTableWidgetItem(names.at(row)));
		setItem(row, 0, new QTableWidgetItem(QStringLiteral("0")));
	}
	setVerticalHeaderItem(names.size(), new QTableWidgetItem(i18nc("@title:row", "Par")));
	setItem(names.size(), 0, new QTableWidgetItem(QStringLiteral("0")));
	doUpdateHeight();
}

void ScoreBoard::setOnlineColors(const QJsonArray &roster)
{
	for (int row = 0; row < roster.size() && row < rowCount() - 1; ++row) {
		auto *heading = verticalHeaderItem(row);
		if (!heading) continue;
		const auto rgba = roster.at(row).toObject().value(QStringLiteral("resolvedColor")).toString();
		const auto color = Kolf::Online::colorFromRgba(rgba);
		if (!color.isValid()) continue;
		heading->setBackground(QBrush(color));
		heading->setToolTip(rgba);
	}
}

void ScoreBoard::setOnlineSnapshot(const QJsonArray &scores, const QJsonArray &pars, int activePlayer, int currentHole)
{
	int holes = pars.size();
	for (const auto &value : scores)
		holes = qMax(holes, value.toArray().size());
	setColumnCount(holes + 1);
	for (int hole = 0; hole < holes; ++hole)
		setHorizontalHeaderItem(hole, new QTableWidgetItem(QString::number(hole + 1)));
	setHorizontalHeaderItem(holes, new QTableWidgetItem(i18nc("@title:column", "Total")));

	const int playerRows = qMin(scores.size(), rowCount() - 1);
	for (int player = 0; player < playerRows; ++player) {
		const auto row = scores.at(player).toArray();
		int total = 0;
		for (int hole = 0; hole < holes; ++hole) {
			const int score = hole < row.size() ? row.at(hole).toInt() : 0;
			setItem(player, hole, new QTableWidgetItem(QString::number(score)));
			total += score;
		}
		setItem(player, holes, new QTableWidgetItem(QString::number(total)));
	}

	int totalPar = 0;
	for (int hole = 0; hole < holes; ++hole) {
		const int par = hole < pars.size() ? pars.at(hole).toInt() : 0;
		setItem(rowCount() - 1, hole, new QTableWidgetItem(QString::number(par)));
		totalPar += par;
		resizeColumnToContents(hole);
	}
	setItem(rowCount() - 1, holes, new QTableWidgetItem(QString::number(totalPar)));
	resizeColumnToContents(holes);
	if (activePlayer >= 0 && activePlayer < playerRows && currentHole > 0 && currentHole <= holes)
		setCurrentCell(activePlayer, currentHole - 1);
	doUpdateHeight();
}

int ScoreBoard::total(int id, QString &name)
{
	int tot = 0;
	for (int i = 0; i < columnCount() - 1; i++) 
		tot += item(id - 1, i)->text().toInt();
		
	name = verticalHeaderItem(id - 1)->text();

	//kDebug(12007) << "tot is" << tot;
	return tot;
}

void ScoreBoard::doUpdateHeight()
{
	int height = 0;
	
	height += horizontalHeader()->height();
	for (int i = 0; i < qMin(3, rowCount()); ++i) height += verticalHeader()->sectionSize(i);
	height += size().height() - horizontalHeader()->height() - viewport()->height();
	setFixedHeight(height);
}

#include "moc_scoreboard.cpp"
