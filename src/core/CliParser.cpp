#include "CliParser.h"

#include <QCommandLineOption>
#include <QCommandLineParser>

namespace ModeFlow::Core {

CliParser::Result CliParser::parse(const QStringList& arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("ModeFlow System Utility"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption optRegister(TaskArgs::Register.toString(), "Register the autostart task in Task Scheduler.");
    QCommandLineOption optUnregister(TaskArgs::Unregister.toString(), "Remove the autostart task from Task Scheduler.");
    QCommandLineOption optLogon(TaskArgs::Logon.toString(), "Started via Task Scheduler (logon).");
    QCommandLineOption optLog(TaskArgs::Log.toString(), "Enable diagnostic logging.");
    QCommandLineOption optDelay(TaskArgs::Delay.toString(), "Set logon delay in seconds.", "seconds", "0");
    QCommandLineOption optSilent(TaskArgs::SilentRestart.toString(), "Internal silent restart flag.");

    parser.addOption(optRegister);
    parser.addOption(optUnregister);
    parser.addOption(optLogon);
    parser.addOption(optLog);
    parser.addOption(optDelay);
    parser.addOption(optSilent);

    Result result;

    if (!parser.parse(arguments)) {
        result.outcome = Outcome::Failed;
        result.text = parser.errorText();
        return result;
    }

    if (parser.isSet(QStringLiteral("help"))) {
        result.outcome = Outcome::ShowHelp;
        result.text = parser.helpText();
        return result;
    }

    if (parser.isSet(QStringLiteral("version"))) {
        result.outcome = Outcome::ShowVersion;
        result.text = QCoreApplication::applicationName() + u' ' + QCoreApplication::applicationVersion();
        return result;
    }

    const bool wantsRegister = parser.isSet(optRegister);
    const bool wantsUnregister = parser.isSet(optUnregister);
    if (wantsRegister && wantsUnregister) {
        result.outcome = Outcome::Failed;
        result.text = QObject::tr("--register and --unregister cannot be used together.");
        return result;
    }

    if (parser.isSet(optDelay)) {
        bool isNumber = false;
        const int delay = parser.value(optDelay).toInt(&isNumber);
        if (!isNumber || delay < 0) {
            result.outcome = Outcome::Failed;
            result.text = QObject::tr("--delay expects a non-negative number of seconds.");
            return result;
        }
        result.options.delaySeconds = delay;
    }

    if (wantsRegister) {
        result.mode = RunMode::RegisterTask;
    } else if (wantsUnregister) {
        result.mode = RunMode::UnregisterTask;
    }

    result.options.isLogon = parser.isSet(optLogon);
    result.options.isSilentRestart = parser.isSet(optSilent);
    result.options.enableLogging = parser.isSet(optLog);

    return result;
}

} // namespace ModeFlow::Core
