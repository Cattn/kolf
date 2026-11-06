// SPDX-License-Identifier: GPL-2.0-or-later
#include "serversettings.h"

#include <KLocalizedString>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Kolf::Online {

namespace {
enum Column { NameColumn, StatusColumn, AddressColumn };

QColor statusColor(const ProbeResult &result, const QPalette &palette)
{
    switch (result.status) {
    case ProbeResult::Status::Online: return QColor(0x00, 0x9e, 0x73);
    case ProbeResult::Status::Incompatible: return QColor(0xe6, 0x9f, 0x00);
    case ProbeResult::Status::Unreachable: return QColor(0xd5, 0x5e, 0x00);
    }
    return palette.color(QPalette::PlaceholderText);
}
}

ServerSettingsPage::ServerSettingsPage(const ServerList &servers, QWidget *parent)
    : QWidget(parent)
    , m_servers(servers)
{
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(i18n("Kolf connects to the default server when you choose Play Online. "
        "Add a server to play on your own network or with another community."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *row = new QHBoxLayout;
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("serverSettingsList"));
    m_list->setAccessibleName(i18n("Servers"));
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({i18nc("@title:column", "Server"), i18nc("@title:column", "Status"), i18nc("@title:column", "Address")});
    m_list->header()->setStretchLastSection(true);
    m_list->header()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(StatusColumn, QHeaderView::ResizeToContents);
    row->addWidget(m_list, 1);

    auto *buttons = new QVBoxLayout;
    const auto makeButton = [this, buttons](const QString &icon, const QString &text, const QString &name) {
        auto *button = new QPushButton(QIcon::fromTheme(icon), text, this);
        button->setObjectName(name);
        buttons->addWidget(button);
        return button;
    };
    m_add = makeButton(QStringLiteral("list-add"), i18nc("@action:button", "Add…"), QStringLiteral("serverAdd"));
    m_edit = makeButton(QStringLiteral("document-edit"), i18nc("@action:button", "Edit…"), QStringLiteral("serverEdit"));
    m_remove = makeButton(QStringLiteral("list-remove"), i18nc("@action:button", "Remove"), QStringLiteral("serverRemove"));
    m_setDefault = makeButton(QStringLiteral("starred-symbolic"), i18nc("@action:button", "Set as Default"), QStringLiteral("serverSetDefault"));
    m_test = makeButton(QStringLiteral("network-connect"), i18nc("@action:button", "Test"), QStringLiteral("serverTest"));
    buttons->addStretch();
    row->addLayout(buttons);
    layout->addLayout(row, 1);

    m_details = new QLabel(this);
    m_details->setObjectName(QStringLiteral("serverDetails"));
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); updateDetails(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] { if (m_edit->isEnabled()) editServer(); });
    connect(m_add, &QPushButton::clicked, this, &ServerSettingsPage::addServer);
    connect(m_edit, &QPushButton::clicked, this, &ServerSettingsPage::editServer);
    connect(m_remove, &QPushButton::clicked, this, &ServerSettingsPage::removeServer);
    connect(m_setDefault, &QPushButton::clicked, this, &ServerSettingsPage::setDefaultServer);
    connect(m_test, &QPushButton::clicked, this, [this] {
        const QString url = selectedUrl();
        if (!url.isEmpty()) testServers({url});
    });
    connect(&m_probe, &ServerProbe::finished, this, [this](const ProbeResult &result) {
        m_results.insert(result.url, result);
        rebuild(selectedUrl());
        startNextProbe();
    });

    rebuild(m_servers.defaultUrl());
}

void ServerSettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_testedOnShow) return;
    m_testedOnShow = true;
    QStringList urls;
    for (const auto &server : m_servers.servers()) urls << server.url;
    testServers(urls);
}

void ServerSettingsPage::rebuild(const QString &selectUrl)
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    const QString defaultUrl = m_servers.defaultUrl();
    QTreeWidgetItem *selected = nullptr;
    for (const auto &server : m_servers.servers()) {
        auto *item = new QTreeWidgetItem(m_list);
        QString name = server.name;
        if (server.url == defaultUrl) name = i18nc("server name, marked as default", "%1 (default)", name);
        item->setText(NameColumn, name);
        item->setData(NameColumn, Qt::UserRole, server.url);
        item->setText(AddressColumn, server.url);
        if (server.url == defaultUrl) {
            QFont font = item->font(NameColumn);
            font.setBold(true);
            item->setFont(NameColumn, font);
        }
        if (server.builtIn) item->setToolTip(NameColumn, i18n("Built into Kolf. It can't be removed, but another server can be the default."));
        QString status;
        if (m_probe.isRunning() && m_probe.url() == server.url) {
            status = i18nc("@info:status", "Testing…");
        } else if (m_probeQueue.contains(server.url)) {
            status = i18nc("@info:status", "Waiting…");
        } else if (m_results.contains(server.url)) {
            const auto &result = m_results[server.url];
            status = result.status == ProbeResult::Status::Online ? i18nc("@info:status", "● Online, %1 ms", result.roundTripMs)
                : result.status == ProbeResult::Status::Incompatible ? i18nc("@info:status", "● Incompatible")
                : i18nc("@info:status", "● Unreachable");
            item->setForeground(StatusColumn, statusColor(result, palette()));
            item->setToolTip(StatusColumn, ServerProbe::describe(result));
        } else {
            status = i18nc("@info:status", "Not tested");
        }
        item->setText(StatusColumn, status);
        if (server.url == selectUrl) selected = item;
    }
    m_list->setCurrentItem(selected ? selected : m_list->topLevelItem(0));
    updateButtons();
    updateDetails();
}

QString ServerSettingsPage::selectedUrl() const
{
    auto *item = m_list->currentItem();
    return item ? item->data(NameColumn, Qt::UserRole).toString() : QString();
}

void ServerSettingsPage::updateButtons()
{
    const QString url = selectedUrl();
    const int index = m_servers.indexOf(url);
    m_edit->setEnabled(index > 0);
    m_remove->setEnabled(index > 0);
    m_setDefault->setEnabled(index >= 0 && url != m_servers.defaultUrl());
    m_test->setEnabled(index >= 0 && !(m_probe.isRunning() && m_probe.url() == url));
}

void ServerSettingsPage::updateDetails()
{
    const QString url = selectedUrl();
    if (url.isEmpty()) { m_details->clear(); return; }
    QString text = m_servers.displayName(url) + QStringLiteral(": ");
    if (m_probe.isRunning() && m_probe.url() == url) text += i18n("testing the connection…");
    else if (m_results.contains(url)) text += ServerProbe::describe(m_results[url]);
    else text += i18n("not tested yet.");
    m_details->setText(text);
}

bool ServerSettingsPage::editDialog(const QString &title, QString &name, QString &address, const QString &currentUrl)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("serverEditDialog"));
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *nameEdit = new QLineEdit(name, &dialog);
    nameEdit->setObjectName(QStringLiteral("serverEditName"));
    nameEdit->setPlaceholderText(i18nc("@info:placeholder", "Living room"));
    nameEdit->setMaxLength(40);
    auto *addressEdit = new QLineEdit(address, &dialog);
    addressEdit->setObjectName(QStringLiteral("serverEditAddress"));
    addressEdit->setPlaceholderText(i18nc("@info:placeholder", "kolf.example.org or 192.168.1.20"));
    form->addRow(i18nc("@label:textbox", "Name:"), nameEdit);
    form->addRow(i18nc("@label:textbox", "Address:"), addressEdit);
    layout->addLayout(form);
    auto *feedback = new QLabel(&dialog);
    feedback->setObjectName(QStringLiteral("serverEditFeedback"));
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    const auto validate = [this, addressEdit, feedback, box, currentUrl] {
        const auto normalized = normalizeServerAddress(addressEdit->text());
        QString message;
        bool ok = normalized.isValid();
        if (addressEdit->text().trimmed().isEmpty()) {
            message = i18n("Enter the address your server's host gave you.");
            ok = false;
        } else if (!ok) {
            message = normalized.error;
        } else {
            const QString url = normalized.url.toString();
            const int existing = m_servers.indexOf(url);
            if (existing >= 0 && url != currentUrl) {
                message = i18n("That server is already in the list.");
                ok = false;
            } else {
                message = i18n("Kolf will connect to %1", url);
                if (url.startsWith(QLatin1String("ws://")))
                    message += QLatin1Char(' ') + i18n("(not encrypted; local network only)");
            }
        }
        feedback->setText(message);
        box->button(QDialogButtonBox::Ok)->setEnabled(ok);
    };
    connect(addressEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    (address.isEmpty() ? addressEdit : nameEdit)->setFocus();
    dialog.resize(440, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) return false;
    name = nameEdit->text();
    address = addressEdit->text();
    return true;
}

void ServerSettingsPage::addServer()
{
    QString name, address;
    if (!editDialog(i18nc("@title:window", "Add Server"), name, address, {})) return;
    if (!m_servers.add(name, address)) return;
    const QString url = normalizeServerAddress(address).url.toString();
    rebuild(url);
    testServers({url});
}

void ServerSettingsPage::editServer()
{
    const QString url = selectedUrl();
    const int index = m_servers.indexOf(url);
    if (index <= 0) return;
    QString name = m_servers.servers().at(index).name;
    QString address = url;
    if (!editDialog(i18nc("@title:window", "Edit Server"), name, address, url)) return;
    if (!m_servers.update(url, name, address)) return;
    const QString newUrl = normalizeServerAddress(address).url.toString();
    if (newUrl != url) m_results.remove(url);
    rebuild(newUrl);
    if (newUrl != url) testServers({newUrl});
}

void ServerSettingsPage::removeServer()
{
    const QString url = selectedUrl();
    if (!m_servers.remove(url)) return;
    m_results.remove(url);
    m_probeQueue.removeAll(url);
    rebuild(m_servers.defaultUrl());
}

void ServerSettingsPage::setDefaultServer()
{
    const QString url = selectedUrl();
    if (m_servers.setDefault(url)) rebuild(url);
}

void ServerSettingsPage::testServers(const QStringList &urls)
{
    for (const auto &url : urls)
        if (!m_probeQueue.contains(url) && !(m_probe.isRunning() && m_probe.url() == url)) m_probeQueue.append(url);
    if (!m_probe.isRunning()) startNextProbe();
    else rebuild(selectedUrl());
}

void ServerSettingsPage::startNextProbe()
{
    while (!m_probeQueue.isEmpty()) {
        const QString url = m_probeQueue.takeFirst();
        if (m_servers.indexOf(url) < 0) continue;
        m_probe.probe(url);
        break;
    }
    rebuild(selectedUrl());
}

}
