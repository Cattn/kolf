/*
    Copyright (C) 2002-2005, Jason Katz-Brown <jasonkb@mit.edu>
    Copyright 2010 Stefan Majewsky <majewsky@gmx.net>

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

#ifndef EDITOR_H_INCLUDED
#define EDITOR_H_INCLUDED

#include <QWidget>

#include "game.h"

class QHBoxLayout;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class Config;
class QUndoStack;

class Editor : public QWidget
{
	Q_OBJECT
public:
	explicit Editor(QWidget * = nullptr);
Q_SIGNALS:
	void changed(bool mod);
	void addNewItem(const QString&);
	void duplicateRequested();
	void testRequested();
public Q_SLOTS:
	void setItem(CanvasItem *);
	void setHistory(QUndoStack *history);
	void setProblems(const QStringList &problems);
private Q_SLOTS:
	void paletteActivated(QTreeWidgetItem *item);
	void filterPalette(const QString &text);
private:
	QHBoxLayout *hlayout;
	QLineEdit *m_search;
	QTreeWidget *m_palette;
	QHBoxLayout *m_historyRow;
	class QLabel *m_problems;
	Config *config;
};

#endif
