#include "LogManager.h"

#include <QDir>
#include <QStandardPaths>

#include "Constants.h"
#include "Logging.h"
#include "SystemUtils.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif // Q_OS_WIN

using namespace Qt::StringLiterals;

namespace ModeFlow::Utils {

namespace {
QString simplifyFunction(const char* rawFunction) {
    if (!rawFunction)
        return u"unknown"_s;

    std::string_view func(rawFunction);

    const size_t parenPos = func.find('(');
    if (parenPos != std::string_view::npos) {
        func = func.substr(0, parenPos);
    }

    const size_t lastSpace = func.rfind(' ');
    if (lastSpace != std::string_view::npos) {
        func = func.substr(lastSpace + 1);
    }

    const size_t lastColons = func.rfind("::");
    if (lastColons != std::string_view::npos) {
        const size_t secondLastColons = func.rfind("::", lastColons - 1);
        if (secondLastColons != std::string_view::npos) {
            func = func.substr(secondLastColons + 2);
        }
    }

    return QString::fromUtf8(func.data(), static_cast<int>(func.size()));
}
} // namespace

QFile LogManager::s_logFile;
QMutex LogManager::s_mutex;
bool LogManager::s_enabled = false;
QString LogManager::s_cachedLogPath;

QString LogManager::logFilePath() {
    QMutexLocker locker(&s_mutex);
    if (!s_cachedLogPath.isEmpty()) {
        return s_cachedLogPath;
    }

    const QString appDir = SystemUtils::getExecutableDir();
    const QString testPath = appDir + u"/.write_test.tmp"_s;
    const QString targetPath = appDir + u"/log.txt"_s;

    // Test if executable directory is writable using a temporary test file
    QFile testFile(testPath);
    if (testFile.open(QIODevice::WriteOnly)) {
        testFile.close();
        QFile::remove(testPath); // Clean up test file immediately
        s_cachedLogPath = targetPath;
        return s_cachedLogPath;
    }

    // Fallback for restricted install directories (e.g. Program Files): use AppData/Roaming/ModeFlow
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);
    s_cachedLogPath = QDir::toNativeSeparators(appDataDir + u"/log.txt"_s);

    return s_cachedLogPath;
}

void LogManager::setup(bool enabled) {
    {
        QMutexLocker locker(&s_mutex);
        s_enabled = enabled;
    }

    if (!enabled) {
        QMutexLocker locker(&s_mutex);
        if (s_logFile.isOpen()) {
            s_logFile.close();
        }
        return;
    }

    const QString logPath = logFilePath();

    bool alreadyLoggingToFile = false;
    bool openFailed = false;
    {
        QMutexLocker locker(&s_mutex);

        // Rotation (if > 5MB)
        if (QFile(logPath).size() > MaxLogFileSizeBytes) {
            QFile::remove(logPath);
        }

        if (s_logFile.isOpen()) {
            if (s_logFile.fileName() == logPath) {
                alreadyLoggingToFile = true;
            } else {
                s_logFile.close();
            }
        }

        if (!alreadyLoggingToFile) {
            s_logFile.setFileName(logPath);
            openFailed = !s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
    }

    // The message handler locks the same mutex and installing it flushes Qt's buffered messages
    // through it, so both must happen only after this thread has released the lock.
    if (openFailed) {
        qCWarning(lcUtil) << "Failed to open log file:" << logPath;
    }

    qInstallMessageHandler(LogManager::messageHandler);
}

void LogManager::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    // Enabled state and the log file belong to one snapshot: worker threads keep emitting while
    // the GUI toggles logging, so reading either of them outside the mutex is a data race.
    QMutexLocker locker(&s_mutex);
    if (!s_enabled || !s_logFile.isOpen())
        return;

    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    QString typeStr;
    switch (type) {
    case QtDebugMsg:
        typeStr = "DEBUG";
        break;
    case QtInfoMsg:
        typeStr = "INFO ";
        break;
    case QtWarningMsg:
        typeStr = "WARN ";
        break;
    case QtCriticalMsg:
        typeStr = "CRIT ";
        break;
    case QtFatalMsg:
        typeStr = "FATAL";
        break;
    }

    QString func = simplifyFunction(context.function);

    QString formattedMsg =
        u"["_s % time % u"] ["_s % typeStr % u"] ["_s % context.category % u"] ["_s % func % u"] "_s % msg % u"\n"_s;

    OutputDebugStringW(reinterpret_cast<const wchar_t*>(formattedMsg.utf16()));

    QTextStream out(&s_logFile);
    out << formattedMsg;
    out.flush();
}

} // namespace ModeFlow::Utils
