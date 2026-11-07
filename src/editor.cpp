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

#include "editor.h"
#include "objectregistry.h"
#include "ui/propertypanel.h"
#include "game.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <KGameRenderer>
#include <KLocalizedString>

namespace
{
QString categoryLabel(Kolf::ObjectCategory category)
{
	switch (category) {
	case Kolf::ObjectCategory::Course: return i18nc("@title:group object palette", "Course");
	case Kolf::ObjectCategory::Terrain: return i18nc("@title:group object palette", "Terrain");
	case Kolf::ObjectCategory::Obstacle: return i18nc("@title:group object palette", "Obstacles");
	case Kolf::ObjectCategory::Hazard: return i18nc("@title:group object palette", "Hazards");
	case Kolf::ObjectCategory::Mechanism: return i18nc("@title:group object palette", "Mechanisms");
	case Kolf::ObjectCategory::Decoration: return i18nc("@title:group object palette", "Decoration");
	}
	return QString();
}

QIcon typeIcon(const Kolf::ObjectType &type)
{
	const int size = 24;
	QIcon icon = type.icon(size);
	if (!icon.isNull())
		return icon;
	if (!type.spriteKey().isEmpty())
		return QIcon(Kolf::renderer()->spritePixmap(type.spriteKey(), QSize(size, size)));
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(Qt::darkRed, 3));
	painter.drawLine(4, size - 6, size - 4, 6);
	return QIcon(pixmap);
}
}

Editor::Editor(QWidget *parent)
	: QWidget(parent)
	, config(nullptr)
{
	setMaximumHeight(fontMetrics().height() * 16);
	hlayout = new QHBoxLayout(this);

	auto *vlayout = new QVBoxLayout;
	hlayout->addLayout(vlayout);
	m_historyRow = new QHBoxLayout;
	vlayout->addLayout(m_historyRow);
	vlayout->addWidget(new QLabel(i18nc("@label:listbox", "Add object:"), this));
	m_search = new QLineEdit(this);
	m_search->setObjectName(QStringLiteral("editorSearch"));
	m_search->setPlaceholderText(i18nc("@info:placeholder", "Search objects"));
	m_search->setClearButtonEnabled(true);
	vlayout->addWidget(m_search);
	m_palette = new QTreeWidget(this);
	m_palette->setObjectName(QStringLiteral("editorPalette"));
	m_palette->setHeaderHidden(true);
	m_palette->setRootIsDecorated(false);
	m_palette->setIconSize(QSize(24, 24));
	vlayout->addWidget(m_palette);
	hlayout->setStretchFactor(vlayout, 2);

	const QList<Kolf::ObjectCategory> order{Kolf::ObjectCategory::Course, Kolf::ObjectCategory::Terrain,
		Kolf::ObjectCategory::Obstacle, Kolf::ObjectCategory::Hazard, Kolf::ObjectCategory::Mechanism, Kolf::ObjectCategory::Decoration};
	auto types = Kolf::ObjectRegistry::instance().types();
	std::sort(types.begin(), types.end(), [](const Kolf::ObjectType &a, const Kolf::ObjectType &b) {
		return QString::localeAwareCompare(a.name(), b.name()) < 0;
	});
	for (const auto category : order) {
		QTreeWidgetItem *group = nullptr;
		for (const auto &type : std::as_const(types)) {
			if (type.objectCategory() != category)
				continue;
			if (!group) {
				group = new QTreeWidgetItem(m_palette, {categoryLabel(category)});
				group->setFlags(Qt::ItemIsEnabled);
				QFont font = group->font(0);
				font.setBold(true);
				group->setFont(0, font);
			}
			auto *entry = new QTreeWidgetItem(group, {type.name()});
			entry->setData(0, Qt::UserRole, type.id());
			entry->setIcon(0, typeIcon(type));
			entry->setToolTip(0, type.helpText());
		}
	}
	m_palette->expandAll();
	connect(m_palette, &QTreeWidget::itemClicked, this, &Editor::paletteActivated);
	connect(m_search, &QLineEdit::textChanged, this, &Editor::filterPalette);
}

void Editor::setHistory(QUndoStack *history)
{
	auto *undo = history->createUndoAction(this, i18nc("@action:button", "Undo"));
	undo->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
	auto *redo = history->createRedoAction(this, i18nc("@action:button", "Redo"));
	redo->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo")));
	auto *duplicate = new QAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:button", "Duplicate"), this);
	duplicate->setToolTip(i18nc("@info:tooltip", "Duplicate the selected object (Ctrl+D)"));
	connect(duplicate, &QAction::triggered, this, &Editor::duplicateRequested);
	const QList<QPair<QAction *, QString>> actions{{undo, QStringLiteral("editorUndo")}, {redo, QStringLiteral("editorRedo")}, {duplicate, QStringLiteral("editorDuplicate")}};
	for (const auto &[action, name] : actions) {
		auto *button = new QToolButton(this);
		button->setObjectName(name);
		button->setDefaultAction(action);
		button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		button->setAutoRaise(true);
		m_historyRow->addWidget(button);
	}
	m_historyRow->addStretch(1);
}

void Editor::paletteActivated(QTreeWidgetItem *item)
{
	const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
	if (!id.isEmpty())
		Q_EMIT addNewItem(id);
}

void Editor::filterPalette(const QString &text)
{
	for (int i = 0; i < m_palette->topLevelItemCount(); ++i) {
		auto *group = m_palette->topLevelItem(i);
		bool anyVisible = false;
		for (int j = 0; j < group->childCount(); ++j) {
			auto *entry = group->child(j);
			const bool match = text.isEmpty() || entry->text(0).contains(text, Qt::CaseInsensitive)
				|| entry->toolTip(0).contains(text, Qt::CaseInsensitive);
			entry->setHidden(!match);
			anyVisible = anyVisible || match;
		}
		group->setHidden(!anyVisible);
	}
}

void Editor::setItem(CanvasItem *item)
{
	delete config;
	config = nullptr;
	const auto *type = item ? Kolf::ObjectRegistry::instance().find(item->name()) : nullptr;
	if (type && !type->propertySpecs().isEmpty())
		config = new Kolf::Ui::PropertyPanel(item, *type, this);
	else if (item)
		config = item->config(this);
	if (!config)
		return;
	config->ctorDone();
	hlayout->addWidget(config);
	hlayout->setStretchFactor(config, 2);
	config->setFrameStyle(QFrame::Box | QFrame::Raised);
	config->setLineWidth(1);
	config->show();
	connect(config, &Config::modified, this, &Editor::changed);
}

#include "moc_editor.cpp"
