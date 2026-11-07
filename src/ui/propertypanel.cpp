// SPDX-License-Identifier: GPL-2.0-or-later
#include "propertypanel.h"

#include "canvasitem.h"
#include "objectregistry.h"

#include <KLocalizedString>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace
{
int decimalsFor(double step)
{
	int decimals = 0;
	while (decimals < 4 && std::abs(step * std::pow(10.0, decimals) - std::round(step * std::pow(10.0, decimals))) > 1e-9)
		++decimals;
	return decimals;
}
}

Kolf::Ui::PropertyPanel::PropertyPanel(CanvasItem *item, const ObjectType &type, QWidget *parent)
	: Config(parent)
	, m_item(item)
{
	setObjectName(QStringLiteral("propertyPanel"));
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(6, 6, 6, 6);
	auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(type.name().toHtmlEscaped()), this);
	outer->addWidget(title);
	if (!type.helpText().isEmpty()) {
		auto *help = new QLabel(type.helpText(), this);
		help->setWordWrap(true);
		help->setForegroundRole(QPalette::PlaceholderText);
		outer->addWidget(help);
	}

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *form = new QWidget(scroll);
	auto *layout = new QFormLayout(form);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(form);
	outer->addWidget(scroll, 1);

	const QVariantMap values = item->properties();
	for (const auto &spec : type.propertySpecs()) {
		const QString key = spec.key();
		const QVariant value = values.value(key, spec.defaultValue());
		QWidget *editor = nullptr;
		switch (spec.type()) {
		case PropertyType::Number: {
			auto *box = new QDoubleSpinBox(form);
			const QJsonObject json = spec.toJson();
			box->setDecimals(decimalsFor(json.value(QStringLiteral("step")).toDouble(1)));
			box->setRange(spec.minimum(), spec.maximum());
			box->setSingleStep(json.value(QStringLiteral("step")).toDouble(1));
			box->setValue(value.toDouble());
			connect(box, &QDoubleSpinBox::valueChanged, this, [this, key](double v) { apply(key, v); });
			editor = box;
			break;
		}
		case PropertyType::Int:
		case PropertyType::Angle: {
			auto *box = new QSpinBox(form);
			box->setRange(int(spec.minimum()), int(spec.maximum()));
			if (spec.type() == PropertyType::Angle) {
				box->setWrapping(true);
				box->setSuffix(i18nc("@item:valuesuffix degrees", "°"));
			}
			box->setValue(value.toInt());
			connect(box, &QSpinBox::valueChanged, this, [this, key](int v) { apply(key, v); });
			editor = box;
			break;
		}
		case PropertyType::Bool: {
			auto *box = new QCheckBox(form);
			box->setChecked(value.toBool());
			connect(box, &QCheckBox::toggled, this, [this, key](bool v) { apply(key, v); });
			editor = box;
			break;
		}
		case PropertyType::Choice: {
			auto *box = new QComboBox(form);
			box->addItems(spec.choices());
			box->setCurrentText(value.toString());
			connect(box, &QComboBox::currentTextChanged, this, [this, key](const QString &v) { apply(key, v); });
			editor = box;
			break;
		}
		case PropertyType::Text: {
			auto *line = new QLineEdit(value.toString(), form);
			line->setMaxLength(int(spec.maximum()));
			connect(line, &QLineEdit::textEdited, this, [this, key](const QString &v) { apply(key, v); });
			editor = line;
			break;
		}
		case PropertyType::Point:
		case PropertyType::PointF: {
			auto *pair = new QWidget(form);
			auto *row = new QHBoxLayout(pair);
			row->setContentsMargins(0, 0, 0, 0);
			auto *x = new QDoubleSpinBox(pair), *y = new QDoubleSpinBox(pair);
			const bool integral = spec.type() == PropertyType::Point;
			for (auto *box : {x, y}) {
				box->setDecimals(integral ? 0 : 1);
				box->setRange(spec.minimum(), spec.maximum());
				row->addWidget(box);
			}
			const QPointF point = value.toPointF();
			x->setValue(point.x());
			y->setValue(point.y());
			const auto update = [this, key, x, y, integral] {
				apply(key, integral ? QVariant(QPoint(int(x->value()), int(y->value()))) : QVariant(QPointF(x->value(), y->value())));
			};
			connect(x, &QDoubleSpinBox::valueChanged, this, update);
			connect(y, &QDoubleSpinBox::valueChanged, this, update);
			editor = pair;
			break;
		}
		}
		editor->setObjectName(QStringLiteral("property_%1").arg(key));
		layout->addRow(spec.label(), editor);
	}
}

void Kolf::Ui::PropertyPanel::apply(const QString &key, const QVariant &value)
{
	m_item->applyProperties({{key, value}});
	changed();
}

#include "moc_propertypanel.cpp"
