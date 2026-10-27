#pragma once

#include <QFile>
#include <QMutex>

namespace ModeFlow::Utils {

class LogManager {
    Q_DISABLE_COPY_MOVE(LogManager)

public:
    static void setup(bool enabled);
    static bool isEnabled();
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    static QString logFilePath();

private:
    static QFile s_logFile;
    static QMutex s_mutex;
    static bool s_enabled;
    static QString s_cachedLogPath;
};

} // namespace ModeFlow::Utils