// SPDX-License-Identifier: GPL-2.0-or-later
#include "playerprofileeditor.h"
#include "color.h"

#include <KColorButton>
#include <KLocalizedString>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>

using namespace Kolf::Online;

PlayerProfileEditor::PlayerProfileEditor(QWidget *parent) : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    m_name = new QLineEdit(this);
    m_name->setMaxLength(32);
    form->addRow(i18nc("@label", "Display name:"), m_name);
    m_choice = new QComboBox(this);
    m_choice->addItem(i18nc("@item:inlistbox", "Auto (assigned in lobby)"), QStringLiteral("auto"));
    const QStringList names = {i18n("Blue"), i18n("Orange"), i18n("Pink"), i18n("Vermilion"),
        i18n("Sky blue"), i18n("Purple"), i18n("Magenta"), i18n("Charcoal")};
    for (int index = 0; index < 8; ++index) {
        const auto rgba = presetColors().at(index);
        QPixmap swatch(20, 20); swatch.fill(colorFromRgba(rgba));
        m_choice->addItem(QIcon(swatch), names.at(index), rgba);
    }
    m_choice->addItem(i18nc("@item:inlistbox", "Custom…"), QStringLiteral("custom"));
    form->addRow(i18nc("@label", "Ball color:"), m_choice);
    m_custom = new KColorButton(colorFromRgba(presetColors().first()), this);
    m_custom->setAlphaChannelEnabled(true);
    m_custom->setAccessibleName(i18n("Choose custom ball color, including transparency"));
    form->addRow(i18nc("@label", "Custom color:"), m_custom);
    m_preview = new QLabel(this);
    m_preview->setAccessibleName(i18n("Ball color preview"));
    form->addRow(i18nc("@label", "Preview:"), m_preview);
    connect(m_choice, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updatePreview(); });
    connect(m_custom, &KColorButton::changed, this, [this] { updatePreview(); });
    updatePreview();
}

void PlayerProfileEditor::setProfile(const QString &name, const QString &mode, const QString &customColor)
{
    m_name->setText(name);
    const QColor selected = colorFromRgba(customColor);
    if (selected.isValid()) m_custom->setColor(selected);
    int index = 0;
    if (mode == QLatin1String("custom")) {
        index = m_choice->findData(customColor.toLower());
        if (index < 0) index = m_choice->findData(QStringLiteral("custom"));
    }
    m_choice->setCurrentIndex(index);
    updatePreview();
}

QString PlayerProfileEditor::playerName() const { return m_name->text().trimmed(); }
QString PlayerProfileEditor::colorMode() const { return m_choice->currentIndex() == 0 ? QStringLiteral("auto") : QStringLiteral("custom"); }
QString PlayerProfileEditor::customColor() const
{
    if (colorMode() == QLatin1String("auto")) return {};
    return m_choice->currentData().toString() == QLatin1String("custom")
        ? rgbaFromColor(m_custom->color()) : m_choice->currentData().toString();
}
bool PlayerProfileEditor::isValid() const { return !playerName().isEmpty(); }

void PlayerProfileEditor::updatePreview()
{
    const bool custom = m_choice->currentData().toString() == QLatin1String("custom");
    m_custom->setVisible(custom);
    if (colorMode() == QLatin1String("auto")) {
        m_preview->setText(i18n("Assigned in lobby"));
        m_preview->setToolTip(i18n("The server assigns an available color when you join the lobby."));
        m_preview->setAccessibleDescription(m_preview->toolTip());
        return;
    }
    const QString rgba = customColor();
    QPixmap swatch(40, 24);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.fillRect(0, 0, 20, 12, Qt::lightGray);
    painter.fillRect(20, 12, 20, 12, Qt::lightGray);
    painter.fillRect(20, 0, 20, 12, Qt::white);
    painter.fillRect(0, 12, 20, 12, Qt::white);
    painter.fillRect(swatch.rect(), colorFromRgba(rgba));
    painter.end();
    m_preview->setPixmap(swatch);
    m_preview->setToolTip(rgba);
    m_preview->setAccessibleDescription(m_preview->toolTip());
}
