// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFile>
#include <QJsonArray>
#include <QObject>
#include <QPointer>

class QWidget;

namespace Kolf::Testing {
class UiScript : public QObject {
    Q_OBJECT
public:
    UiScript(const QString &scriptPath, QWidget *mainWindow, QObject *parent = nullptr);
    void start();

private:
    void runNext();
    bool runStep(const QJsonObject &step, QString &error);
    QWidget *target(const QString &name) const;
    void log(const QJsonObject &entry);
    void finish(int exitCode, const QString &error = {});

    QPointer<QWidget> m_mainWindow;
    QString m_outputDirectory;
    QString m_scriptDirectory;
    QJsonArray m_steps;
    QFile m_log;
    int m_index = 0;
};
}
