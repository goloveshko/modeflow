#include "CommandLineBuilder.h"

#include <QStringBuilder>
#include <QStringList>

#include "CliArgs.h"

namespace ModeFlow::Core {

using namespace Qt::StringLiterals;

CommandLineBuilder& CommandLineBuilder::withLogon(bool enable) {
    m_logon = enable;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withLog(bool enable) {
    m_log = enable;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withRegister(bool enable) {
    m_register = enable;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withUnregister(bool enable) {
    m_unregister = enable;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withSilentRestart(bool enable) {
    m_silentRestart = enable;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withDelay(int seconds) {
    m_delaySeconds = seconds;
    return *this;
}

CommandLineBuilder& CommandLineBuilder::withForUser(const QString& sid) {
    m_forUserSid = sid;
    return *this;
}

QStringList CommandLineBuilder::toStringList() const {
    QStringList args;
    if (m_logon)
        args << u"--"_s % TaskArgs::Logon;
    if (m_log)
        args << u"--"_s % TaskArgs::Log;
    if (m_register)
        args << u"--"_s % TaskArgs::Register;
    if (m_unregister)
        args << u"--"_s % TaskArgs::Unregister;
    if (m_silentRestart)
        args << u"--"_s % TaskArgs::SilentRestart;
    if (m_delaySeconds > 0) {
        args << u"--"_s % TaskArgs::Delay << QString::number(m_delaySeconds);
    }
    if (!m_forUserSid.isEmpty()) {
        args << u"--"_s % TaskArgs::ForUser << m_forUserSid;
    }
    return args;
}

QString CommandLineBuilder::toString() const {
    return toStringList().join(u' ');
}

} // namespace ModeFlow::Core
