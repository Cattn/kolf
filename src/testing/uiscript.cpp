// SPDX-License-Identifier: GPL-2.0-or-later
#include "uiscript.h"

#include <KXmlGuiWindow>
#include <KActionCollection>
#include <KPageDialog>
#include <KPageView>
#include <KPageWidgetModel>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QLineEdit>
#include <QKeySequence>
#include <QMenu>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>

#include "game.h"

using namespace Kolf::Testing;

UiScript::UiScript(const QString &scriptPath, QWidget *mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
    QFile file(scriptPath);
    QJsonObject script;
    if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024)
        script = QJsonDocument::fromJson(file.readAll()).object();
    m_steps = script.value(QStringLiteral("steps")).toArray();
    m_outputDirectory = qEnvironmentVariable("KOLF_UI_TEST_OUTPUT", script.value(QStringLiteral("outputDirectory")).toString());
    m_scriptDirectory = QFileInfo(scriptPath).absolutePath();
    if (m_outputDirectory.isEmpty()) m_outputDirectory = m_scriptDirectory;
    QDir().mkpath(m_outputDirectory);
    m_log.setFileName(QDir(m_outputDirectory).filePath(QStringLiteral("ui-script.jsonl")));
    m_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

void UiScript::start()
{
    if (m_steps.isEmpty()) { finish(2, QStringLiteral("UI script has no steps or could not be read")); return; }
    QTimer::singleShot(0, this, &UiScript::runNext);
}

void UiScript::log(const QJsonObject &entry)
{
    m_log.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
    m_log.flush();
}

void UiScript::finish(int exitCode, const QString &error)
{
    if (!error.isEmpty()) {
        qCritical() << error;
        log({{QStringLiteral("event"), QStringLiteral("failed")}, {QStringLiteral("step"), m_index},
            {QStringLiteral("error"), error}});
    } else {
        log({{QStringLiteral("event"), QStringLiteral("finished")}});
    }
    QTimer::singleShot(0, qApp, [exitCode] { QCoreApplication::exit(exitCode); });
}

QWidget *UiScript::target(const QString &name) const
{
    if (name.isEmpty() || name == QLatin1String("window")) return m_mainWindow;
    if (name == QLatin1String("modal")) return QApplication::activeModalWidget();
    if (name == QLatin1String("focus")) return QApplication::focusWidget();
    if (name == QLatin1String("active")) return QApplication::activeWindow();
    if (name == QLatin1String("game")) {
        KolfGame *game = nullptr;
        if (m_mainWindow)
            for (auto *candidate : m_mainWindow->findChildren<KolfGame *>())
                if (candidate->isVisible() && !candidate->isPaused()) game = candidate;
        return game;
    }
    for (auto *topLevel : QApplication::topLevelWidgets()) {
        if (topLevel->objectName() == name) return topLevel;
        if (auto *child = topLevel->findChild<QWidget *>(name)) return child;
    }
    return nullptr;
}

void UiScript::runNext()
{
    if (m_index >= m_steps.size()) { finish(0); return; }
    const auto step = m_steps.at(m_index).toObject();
    QString error;
    if (!runStep(step, error)) {
        const int within = step.value(QStringLiteral("within")).toInt();
        if (within > 0) {
            if (!m_stepClock.isValid()) m_stepClock.start();
            if (m_stepClock.elapsed() < within) { QTimer::singleShot(100, this, &UiScript::runNext); return; }
        }
        m_stepClock.invalidate();
        if (!step.value(QStringLiteral("continueOnError")).toBool()) { finish(3, error); return; }
        log({{QStringLiteral("event"), QStringLiteral("skipped")}, {QStringLiteral("step"), m_index}, {QStringLiteral("error"), error}});
    }
    m_stepClock.invalidate();
    const auto *focus = QApplication::focusWidget();
    log({{QStringLiteral("event"), QStringLiteral("step")}, {QStringLiteral("step"), m_index}, {QStringLiteral("spec"), step},
        {QStringLiteral("focus"), focus ? QStringLiteral("%1/%2").arg(QString::fromLatin1(focus->metaObject()->className()), focus->objectName()) : QString()}});
    if (step.value(QStringLiteral("quit")).toBool()) { finish(0); return; }
    ++m_index;
    const int delay = step.value(QStringLiteral("wait")).toInt(150);
    QTimer::singleShot(qBound(0, delay, 60000), this, &UiScript::runNext);
}

bool UiScript::runStep(const QJsonObject &step, QString &error)
{
    const auto expand = [this](QString text) {
        const auto courses = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("courses"),
            QStandardPaths::LocateDirectory);
        return text.replace(QStringLiteral("{output}"), QDir(m_outputDirectory).absolutePath())
            .replace(QStringLiteral("{courses}"), courses)
            .replace(QStringLiteral("{scriptDir}"), m_scriptDirectory);
    };
    if (step.contains(QStringLiteral("writeFile"))) {
        QFile file(QDir(m_outputDirectory).filePath(step.value(QStringLiteral("writeFile")).toString()));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = QStringLiteral("cannot write %1").arg(file.fileName()); return false; }
        file.write(expand(step.value(QStringLiteral("content")).toString()).toUtf8());
    }
    if (step.contains(QStringLiteral("setConfig"))) {
        const auto entry = step.value(QStringLiteral("setConfig")).toObject();
        KConfigGroup group(KSharedConfig::openConfig(), entry.value(QStringLiteral("group")).toString());
        group.writeEntry(entry.value(QStringLiteral("key")).toString().toUtf8().constData(),
            expand(entry.value(QStringLiteral("value")).toString()));
        group.sync();
    }
    if (step.contains(QStringLiteral("action"))) {
        auto *window = qobject_cast<KXmlGuiWindow *>(m_mainWindow.data());
        auto *action = window ? window->actionCollection()->action(step.value(QStringLiteral("action")).toString()) : nullptr;
        if (!action) { error = QStringLiteral("unknown action %1").arg(step.value(QStringLiteral("action")).toString()); return false; }
        if (!action->isEnabled()) { error = QStringLiteral("action %1 is disabled").arg(action->objectName()); return false; }
        QTimer::singleShot(0, action, &QAction::trigger);
    }
    if (step.contains(QStringLiteral("click"))) {
        const auto name = step.value(QStringLiteral("click")).toString();
        auto *button = qobject_cast<QAbstractButton *>(target(name));
        if (!button || !button->isVisible() || !button->isEnabled()) {
            error = QStringLiteral("button %1 is missing, hidden, or disabled").arg(name); return false;
        }
        QTimer::singleShot(0, button, &QAbstractButton::click);
    }
    if (step.contains(QStringLiteral("type"))) {
        auto *edit = qobject_cast<QLineEdit *>(target(step.value(QStringLiteral("into")).toString(QStringLiteral("focus"))));
        if (!edit || !edit->isVisible() || !edit->isEnabled()) { error = QStringLiteral("no editable field to type into"); return false; }
        edit->setFocus();
        edit->selectAll();
        edit->insert(step.value(QStringLiteral("type")).toString());
    }
    if (step.contains(QStringLiteral("clickText"))) {
        const auto text = step.value(QStringLiteral("clickText")).toString();
        QWidget *scope = target(step.value(QStringLiteral("in")).toString(QStringLiteral("modal")));
        QAbstractButton *match = nullptr;
        if (scope) {
            for (auto *button : scope->findChildren<QAbstractButton *>()) {
                if (button->isVisible() && button->text().remove(QLatin1Char('&')) == text) { match = button; break; }
            }
        }
        if (!match) { error = QStringLiteral("no visible button labelled %1").arg(text); return false; }
        QTimer::singleShot(0, match, &QAbstractButton::click);
    }
    if (step.contains(QStringLiteral("key"))) {
        const QKeySequence sequence(step.value(QStringLiteral("key")).toString());
        QWidget *receiver = target(step.value(QStringLiteral("to")).toString(QStringLiteral("focus")));
        if (sequence.isEmpty() || !receiver) { error = QStringLiteral("cannot send key %1").arg(step.value(QStringLiteral("key")).toString()); return false; }
        const auto combination = sequence[0];
        QTimer::singleShot(0, receiver, [receiver = QPointer<QWidget>(receiver), combination] {
            if (!receiver) return;
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
            QCoreApplication::sendEvent(receiver, &press);
            if (!receiver) return;
            QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
            QCoreApplication::sendEvent(receiver, &release);
        });
    }
    if (step.contains(QStringLiteral("drag"))) {
        const auto points = step.value(QStringLiteral("drag")).toArray();
        auto *game = qobject_cast<KolfGame *>(target(QStringLiteral("game")));
        if (points.size() != 4 || !game) { error = QStringLiteral("drag needs [x1, y1, x2, y2] board coordinates and a visible course"); return false; }
        const QPointF from(points.at(0).toDouble(), points.at(1).toDouble()), to(points.at(2).toDouble(), points.at(3).toDouble());
        QTimer::singleShot(0, game, [game = QPointer<KolfGame>(game), from, to] {
            const auto send = [&game](QEvent::Type type, const QPointF &board, Qt::MouseButtons buttons) {
                if (!game) return;
                const QPointF view = game->viewPointForBoard(board);
                QMouseEvent event(type, view, game->viewport()->mapToGlobal(view), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    buttons, Qt::NoModifier);
                QCoreApplication::sendEvent(game->viewport(), &event);
            };
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
                send(QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
        });
    }
    if (step.contains(QStringLiteral("openMenu"))) {
        const auto name = step.value(QStringLiteral("openMenu")).toString();
        auto *menu = m_mainWindow ? m_mainWindow->findChild<QMenu *>(name) : nullptr;
        if (!menu) { error = QStringLiteral("no menu named %1").arg(name); return false; }
        QTimer::singleShot(0, menu, [menu, window = m_mainWindow] {
            if (window) menu->popup(window->mapToGlobal(QPoint(40, 40)));
        });
    }
    if (step.contains(QStringLiteral("activateItem"))) {
        const auto text = step.value(QStringLiteral("activateItem")).toString();
        auto *tree = m_mainWindow ? m_mainWindow->findChild<QTreeWidget *>(step.value(QStringLiteral("in")).toString()) : nullptr;
        const auto found = tree ? tree->findItems(text, Qt::MatchExactly | Qt::MatchRecursive) : QList<QTreeWidgetItem *>();
        if (found.isEmpty()) { error = QStringLiteral("no item %1 in %2").arg(text, step.value(QStringLiteral("in")).toString()); return false; }
        QTimer::singleShot(0, tree, [tree, item = found.first()] { Q_EMIT tree->itemClicked(item, 0); });
    }
    if (step.contains(QStringLiteral("trigger"))) {
        const auto text = step.value(QStringLiteral("trigger")).toString();
        auto *menu = m_mainWindow ? m_mainWindow->findChild<QMenu *>(step.value(QStringLiteral("in")).toString()) : nullptr;
        QAction *match = nullptr;
        if (menu)
            for (auto *action : menu->actions())
                if (action->isVisible() && action->isEnabled() && action->text().remove(QLatin1Char('&')) == text) { match = action; break; }
        if (!match) { error = QStringLiteral("no enabled menu item %1").arg(text); return false; }
        menu->hide();
        QTimer::singleShot(0, match, &QAction::trigger);
    }
    if (step.contains(QStringLiteral("closeMenu"))) {
        auto *menu = m_mainWindow ? m_mainWindow->findChild<QMenu *>(step.value(QStringLiteral("closeMenu")).toString()) : nullptr;
        if (menu) menu->hide();
    }
    if (step.contains(QStringLiteral("page"))) {
        const auto name = step.value(QStringLiteral("page")).toString();
        auto *dialog = qobject_cast<KPageDialog *>(QApplication::activeModalWidget());
        auto *model = dialog ? qobject_cast<KPageWidgetModel *>(dialog->findChild<KPageView *>()->model()) : nullptr;
        KPageWidgetItem *match = nullptr;
        for (int row = 0; model && row < model->rowCount() && !match; ++row) {
            auto *item = model->item(model->index(row, 0));
            if (item && item->name() == name) match = item;
        }
        if (!match) { error = QStringLiteral("no settings page named %1").arg(name); return false; }
        dialog->setCurrentPage(match);
    }
    if (step.contains(QStringLiteral("openLink"))) {
        const auto link = step.value(QStringLiteral("openLink")).toString();
        QTimer::singleShot(0, m_mainWindow, [window = m_mainWindow, link] {
            if (window) QMetaObject::invokeMethod(window, "openInviteUrl", Q_ARG(QString, link));
        });
    }
    if (step.contains(QStringLiteral("open"))) {
        const auto url = QUrl::fromLocalFile(QFileInfo(expand(step.value(QStringLiteral("open")).toString())).absoluteFilePath());
        if (!QFileInfo::exists(url.toLocalFile())) { error = QStringLiteral("cannot open %1").arg(url.toLocalFile()); return false; }
        QTimer::singleShot(0, m_mainWindow, [window = m_mainWindow, url] {
            if (window) QMetaObject::invokeMethod(window, "openUrl", Q_ARG(QUrl, url));
        });
    }
    if (step.contains(QStringLiteral("shot"))) {
        const auto shot = step.value(QStringLiteral("shot")).toObject();
        KolfGame *game = nullptr;
        for (auto *candidate : m_mainWindow->findChildren<KolfGame *>())
            if (candidate->isVisible() && !candidate->isPaused()) game = candidate;
        if (!game) { error = QStringLiteral("no active game for a shot"); return false; }
        const Kolf::Session::ShotIntent intent{shot.value(QStringLiteral("direction")).toDouble(),
            shot.value(QStringLiteral("power")).toDouble(1.0), false};
        QTimer::singleShot(0, game, [game = QPointer<KolfGame>(game), intent] {
            if (game) game->applyAcceptedShot(intent);
        });
    }
    if (step.contains(QStringLiteral("resize"))) {
        const auto size = step.value(QStringLiteral("resize")).toArray();
        if (size.size() != 2 || !m_mainWindow) { error = QStringLiteral("resize needs [width, height]"); return false; }
        m_mainWindow->resize(size.at(0).toInt(), size.at(1).toInt());
    }
    if (step.contains(QStringLiteral("expectVisible"))) {
        const auto name = step.value(QStringLiteral("expectVisible")).toString();
        auto *widget = target(name);
        if (!widget || !widget->isVisible()) { error = QStringLiteral("%1 is not visible").arg(name); return false; }
    }
    if (step.contains(QStringLiteral("expectHidden"))) {
        const auto name = step.value(QStringLiteral("expectHidden")).toString();
        auto *widget = target(name);
        if (widget && widget->isVisible()) { error = QStringLiteral("%1 is visible").arg(name); return false; }
    }
    if (step.contains(QStringLiteral("expectTitle"))) {
        const auto expected = step.value(QStringLiteral("expectTitle")).toString();
        if (!m_mainWindow || !m_mainWindow->windowTitle().contains(expected)) {
            error = QStringLiteral("window title %1 does not contain %2").arg(m_mainWindow ? m_mainWindow->windowTitle() : QString(), expected);
            return false;
        }
    }
    if (step.contains(QStringLiteral("screenshot"))) {
        QWidget *widget = target(step.value(QStringLiteral("of")).toString(QStringLiteral("window")));
        if (!widget) { error = QStringLiteral("nothing to screenshot"); return false; }
        const auto path = QDir(m_outputDirectory).filePath(step.value(QStringLiteral("screenshot")).toString());
        if (!widget->grab().save(path)) { error = QStringLiteral("could not save %1").arg(path); return false; }
    }
    return true;
}
