#pragma once

#include <QString>

namespace ModeFlow::Utils {

class TaskScheduler {
public:
    // Register the program in the scheduler
    // arguments - launch arguments
    // principalSid - the account the logon task must run for; empty leaves the task bound to the
    //                registering process, which is wrong when an elevated helper registers on
    //                behalf of a standard user
    // taskName - the name of the task in the list (optional, auto-filled if empty)
    // exePath - the full path to the program (optional, auto-filled if empty)
    static bool createTaskAtLogon(const QString& arguments = "", int delaySeconds = 0, bool runHighest = true,
                                  const QString& principalSid = QString(), QString taskName = QString(),
                                  QString exePath = QString());

    static bool removeTask(QString taskName = QString());

    static bool isTaskRegistered(QString taskName = QString(), QString exePath = QString());

    static bool isAdmin();
    static bool runAsAdmin(const QString& argument);

    static QString currentUserSid();
    static bool isValidUserSid(const QString& sid);

    /// Read access for principalSid plus the rights the scheduler grants administrators and
    /// SYSTEM by default; empty when there is no principal to hand the task to.
    static QString taskSecurityDescriptor(const QString& principalSid);
};
} // namespace ModeFlow::Utils
