#pragma once

#include <QStringView>

namespace ModeFlow::Core {

using namespace Qt::StringLiterals;

namespace TaskArgs {
constexpr auto Register = u"register"_sv;
constexpr auto Unregister = u"unregister"_sv;
constexpr auto Logon = u"logon"_sv;
constexpr auto Log = u"log"_sv;
constexpr auto Delay = u"delay"_sv;
constexpr auto SilentRestart = u"silent-restart"_sv;
constexpr auto ForUser = u"for-user"_sv;
} // namespace TaskArgs

struct StartupOptions {
    bool isLogon = false;
    bool isSilentRestart = false;
    bool enableLogging = false;
    int delaySeconds = 0;
    /// SID of the account the logon task must run for; set only in the elevated register helper.
    QString principalSid;
};

} // namespace ModeFlow::Core
