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

#include "kolf.h"
#include "kolf_version.h"
#include "session/onlineprotocol.h"
#include <QJsonDocument>

#include <iostream>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include <QSettings>
#include <QCoreApplication>
#include <QLocalSocket>
#include <QLocalServer>
#include <QDir>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QUrl>

#include <KAboutData>
#include <KConfig>
#include <KCrash>
#ifdef KOLF_TEST_AUTOMATION
#include "testing/uiscript.h"
#endif
#include <KLocalizedString>
#define HAVE_KICONTHEME __has_include(<KIconTheme>)
#if HAVE_KICONTHEME
#include <KIconTheme>
#endif

#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif
using namespace std;

int main(int argc, char **argv)
{
#if HAVE_KICONTHEME
    KIconTheme::initTheme();
#endif
    QApplication app(argc, argv);
#ifdef KOLF_TEST_AUTOMATION
    if (!qEnvironmentVariableIsEmpty("KOLF_ONLINE_TEST_CONFIG") || !qEnvironmentVariableIsEmpty("KOLF_UI_TEST_SCRIPT"))
        KConfig::setMainConfigName(qEnvironmentVariable("KOLF_TEST_CONFIG_NAME", QStringLiteral("kolf-testrc")));
#endif
#if HAVE_STYLE_MANAGER
    KStyleManager::initStyle();
#else // !HAVE_STYLE_MANAGER
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QApplication::setStyle(QStringLiteral("breeze"));
#endif // defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#endif // HAVE_STYLE_MANAGER
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kolf"));

    KAboutData aboutData( QStringLiteral("kolf"),
			  i18n("Kolf"),
			  QStringLiteral(KOLF_DISPLAY_VERSION),
			  i18n("KDE Minigolf Game"),
			  KAboutLicense::GPL,
			  i18n("(c) 2002-2010, Kolf & Cattn developers"),
			  QString(),
			  QStringLiteral("https://apps.kde.org/kolf"));

	aboutData.addAuthor(i18n("Stefan Majewsky"), i18n("Current maintainer"), QStringLiteral("majewsky@gmx.net"));
	aboutData.addAuthor(i18n("Jason Katz-Brown"), i18n("Former main author"), QStringLiteral("jasonkb@mit.edu"));
	aboutData.addAuthor(i18n("Niklas Knutsson"), i18n("Advanced putting mode"));
	aboutData.addAuthor(i18n("Rik Hemsley"), i18n("Border around course"));
	aboutData.addAuthor(i18n("Timo A. Hummel"), i18n("Some good sound effects"), QStringLiteral("timo.hummel@gmx.net"));
	aboutData.addAuthor(i18n("Cattn"), i18n("Kolf modder!"), QStringLiteral("timo.hummel@gmx.net"));
	aboutData.addAuthor(i18n("Vape"), i18n("idk he's there"), QStringLiteral("timo.hummel@gmx.net"));

	aboutData.addCredit(i18n("Rob Renaud"), i18n("Wall-bouncing help"));
	aboutData.addCredit(i18n("Aaron Seigo"), i18n("Suggestions, bug reports"));
	aboutData.addCredit(i18n("Erin Catto"), i18n("Developer of Box2D physics engine"));
	aboutData.addCredit(i18n("Ryan Cumming"), i18n("Vector class (Kolf 1)"));
	aboutData.addCredit(i18n("Daniel Matza-Brown"), i18n("Working wall-bouncing algorithm (Kolf 1)"));

    KAboutData::setApplicationData(aboutData);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kolf")));

    KCrash::initialize();

    QCommandLineParser parser;
        parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("+file"), i18n("File")));
        parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("course-info"), i18n("Print course information and exit")));

    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    const auto protocolTests = qEnvironmentVariable("KOLF_ONLINE_PROTOCOL_TESTS");
    if (!protocolTests.isEmpty()) return Kolf::Session::runOnlineProtocolFixtures(protocolTests);
#ifdef KOLF_TEST_AUTOMATION
    const auto onlineTestConfig = qEnvironmentVariable("KOLF_ONLINE_TEST_CONFIG");
    if (!onlineTestConfig.isEmpty()) {
        QFile config(onlineTestConfig);
        if (!config.open(QIODevice::ReadOnly) || config.size() > 1024 * 1024) return 2;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(config.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return 2;
        KolfWindow window;
        window.closeGame();
        window.startOnlineAutomation(document.object());
        window.show();
        return app.exec();
    }
#endif

	// I've actually added this for my web site uploaded courses display
	if (parser.isSet(QStringLiteral("course-info")))
	{
		QString filename(parser.value(QStringLiteral("course-info")));
		if (QFile::exists(filename))
		{
			CourseInfo info;
			KolfGame::courseInfo(info, filename);

			cout << info.name.toLatin1().constData() 
			     << " - " << i18n("By %1", info.author).toLatin1().constData()
			     << " - " << i18n("%1 holes", info.holes).toLatin1().constData()
			     << " - " << i18n("par %1", info.par).toLatin1().constData()
			     << endl;

			return 0;
		}
		else
		{
			qCritical() << i18n("Course %1 does not exist.", filename);
		}
	}

	const QString lastArgument = parser.positionalArguments().isEmpty() ? QString() : parser.positionalArguments().constLast();
	const bool inviteArgument = lastArgument.startsWith(QLatin1String("kolf:"), Qt::CaseInsensitive);
	const QString instanceName = QStringLiteral("kolf-invites-%1").arg(QString::fromUtf8(
		QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16)));
	if (inviteArgument) {
		QLocalSocket running;
		running.connectToServer(instanceName);
		if (running.waitForConnected(500)) {
			running.write(lastArgument.toUtf8().left(2048) + '\n');
			running.waitForBytesWritten(1000);
			running.disconnectFromServer();
			return 0;
		}
	}

	KolfWindow *top = new KolfWindow;

	const bool automated = !qEnvironmentVariableIsEmpty("KOLF_UI_TEST_SCRIPT") || !qEnvironmentVariableIsEmpty("KOLF_ONLINE_TEST_CONFIG");
#ifdef Q_OS_WIN
	if (!automated) {
		QSettings scheme(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\kolf"), QSettings::NativeFormat);
		const QString command = QStringLiteral("\"%1\" \"%2\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), QStringLiteral("%1"));
		if (scheme.value(QStringLiteral("shell/open/command/Default")).toString() != command) {
			scheme.setValue(QStringLiteral("Default"), QStringLiteral("URL:Kolf invite"));
			scheme.setValue(QStringLiteral("URL Protocol"), QString());
			scheme.setValue(QStringLiteral("shell/open/command/Default"), command);
		}
	}
#endif
	if (!automated) {
		auto *invites = new QLocalServer(top);
		invites->setSocketOptions(QLocalServer::UserAccessOption);
		if (!invites->listen(instanceName)) {
			QLocalServer::removeServer(instanceName);
			invites->listen(instanceName);
		}
		QObject::connect(invites, &QLocalServer::newConnection, top, [invites, top] {
			while (auto *client = invites->nextPendingConnection()) {
				QObject::connect(client, &QLocalSocket::readyRead, top, [client, top] {
					if (!client->canReadLine()) return;
					const auto link = QString::fromUtf8(client->readLine(2048)).trimmed();
					client->disconnectFromServer();
					QTimer::singleShot(0, top, [top, link] { top->openInviteUrl(link); });
				});
				QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
			}
		});
	}

	if (inviteArgument)
	{
		top->closeGame();
		QTimer::singleShot(0, top, [top, lastArgument] { top->openInviteUrl(lastArgument); });
	}
	else if (parser.positionalArguments().count() >= 1)
	{
		QUrl url = QUrl::fromUserInput(parser.positionalArguments().at(parser.positionalArguments().count() - 1));
		top->openUrl(url);
		
	}
	else
		top->closeGame();

	top->show();
#ifdef KOLF_TEST_AUTOMATION
	const auto uiTestScript = qEnvironmentVariable("KOLF_UI_TEST_SCRIPT");
	if (!uiTestScript.isEmpty())
		(new Kolf::Testing::UiScript(uiTestScript, top, top))->start();
#endif

	return app.exec();
}

