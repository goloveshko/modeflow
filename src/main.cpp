#include <QApplication>
#include <QDirIterator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <cstdio>

#include "AppController.h"
#include "CliParser.h"
#include "ComInitGuard.h"
#include "CommandLineBuilder.h"
#include "ConfigManager.h"
#include "CrashHandler.h"
#include "LogManager.h"
#include "Logging.h"
#include "SingleInstanceGuard.h"
#include "TaskScheduler.h"
#include "VersionInfo.h"

using namespace ModeFlow::Core;
using namespace ModeFlow::Utils;
using namespace Qt::StringLiterals;

namespace {
void listAppResources() {
    qCDebug(lcMain) << "--- Listing all virtual resources ---";
    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto res = it.next();
        if (res.startsWith(":/qt-project.org/"))
            continue;
        qCDebug(lcMain) << res;
    }
    qCDebug(lcMain) << "--- End of resource list ---";
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(ModeFlow::Info::Company);
    QCoreApplication::setApplicationName(ModeFlow::Info::ProductName);
    QCoreApplication::setOrganizationDomain(ModeFlow::Info::Domain);
    QCoreApplication::setApplicationVersion(ModeFlow::Info::Version);

    const CliParser::Result cli = CliParser::parse(app.arguments());
    if (cli.outcome != CliParser::Outcome::Run) {
        // Help, version and bad arguments finish here, before the crash dump directory, the
        // log file and any Task Scheduler operation are reached.
        QTextStream out(cli.outcome == CliParser::Outcome::Failed ? stderr : stdout);
        out << cli.text << '\n';
        return cli.outcome == CliParser::Outcome::Failed ? 1 : 0;
    }

    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    ModeFlow::Utils::CrashHandler::setup(appDataDir + "/CrashDumps");

    app.setWindowIcon(QIcon(u":/icons/app/icon.svg"_s));

    ConfigManager config;
    config.loadConfig();
    LogManager::setup(config.loggingEnabled() || cli.options.enableLogging);

    qCDebug(lcMain) << "--- Log Session Started ---";
    qCDebug(lcMain) << "Application Version:" << ModeFlow::Info::Version;
    qCDebug(lcMain) << "OS Version:" << QSysInfo::prettyProductName();

#ifdef QT_DEBUG
    // listAppResources();
#endif

    qCDebug(lcMain) << "Application started.";

#ifdef Q_OS_WIN
    ModeFlow::Utils::ComInitGuard comGuard;
    if (!comGuard.isOk())
        return 1;
#endif

    if (cli.mode == CliParser::RunMode::RegisterTask) {
        CommandLineBuilder builder;
        builder.withLogon(cli.options.isLogon).withLog(cli.options.enableLogging);

        const bool ok = TaskScheduler::createTaskAtLogon(builder.toString(), cli.options.delaySeconds, false,
                                                         cli.options.principalSid);
        return ok ? 0 : 1;
    }
    if (cli.mode == CliParser::RunMode::UnregisterTask) {
        const bool ok = TaskScheduler::removeTask();
        return ok ? 0 : 1;
    }

    ModeFlow::Services::SingleInstanceGuard guard;
    if (!guard.tryToRun()) {
        qCDebug(lcMain) << "Another instance is already running. Raising it and exiting.";
        return 0;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(0, QObject::tr("System tray"),
                              QObject::tr("The system tray is unavailable on this system."));
        return 1;
    }

    QApplication::setQuitOnLastWindowClosed(false);

    auto appController = std::make_unique<AppController>();

    QObject::connect(&guard, &ModeFlow::Services::SingleInstanceGuard::signalRaiseWindow, appController.get(),
                     &AppController::raiseMainWindow);

    QObject::connect(appController.get(), &AppController::aboutToRestart, &guard,
                     &ModeFlow::Services::SingleInstanceGuard::shutdown);

    appController->init(cli.options);

    return app.exec();
}
