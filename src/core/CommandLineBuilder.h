#pragma once

#include <QStringList>

namespace ModeFlow::Core {

using namespace Qt::StringLiterals;

/**
 * @brief Thread-safe command-line argument builder.
 * Provides a Fluent API for clean parameter assembly.
 */
class CommandLineBuilder {
public:
    CommandLineBuilder() = default;

    CommandLineBuilder& withLogon(bool enable = true);
    CommandLineBuilder& withLog(bool enable = true);
    CommandLineBuilder& withRegister(bool enable = true);
    CommandLineBuilder& withUnregister(bool enable = true);
    CommandLineBuilder& withSilentRestart(bool enable = true);
    CommandLineBuilder& withDelay(int seconds);
    CommandLineBuilder& withForUser(const QString& sid);

    // Collects a list of arguments for QProcess or ShellExecute.
    QStringList toStringList() const;

    // Compiles a flat string for the registry or TaskScheduler
    QString toString() const;

private:
    bool m_logon = false;
    bool m_log = false;
    bool m_register = false;
    bool m_unregister = false;
    bool m_silentRestart = false;
    int m_delaySeconds = 0;
    QString m_forUserSid;
};

} // namespace ModeFlow::Core
