#include "AutostartManager.h"

#include <QtConcurrent>

#include "CommandLineBuilder.h"
#include "TaskScheduler.h"

namespace ModeFlow::Services {

AutostartManager::AutostartManager(QObject* parent) : QObject(parent) {}

bool AutostartManager::isAdmin() {
    return Utils::TaskScheduler::isAdmin();
}

QFuture<bool> AutostartManager::checkIsRegisteredAsync() {
    return QtConcurrent::run([]() { return Utils::TaskScheduler::isTaskRegistered(); });
}

QFuture<bool> AutostartManager::toggleAsync(bool checked, int delaySeconds, bool enableLogging) {
    return QtConcurrent::run([=]() {
        using namespace ModeFlow::Core;

        bool success = false;

        if (Utils::TaskScheduler::isAdmin()) {
            CommandLineBuilder taskArgsBuilder;
            taskArgsBuilder.withLogon().withLog(enableLogging);

            if (checked) {
                success = Utils::TaskScheduler::createTaskAtLogon(taskArgsBuilder.toString(), delaySeconds, false);
            } else {
                success = Utils::TaskScheduler::removeTask();
            }
        } else {
            CommandLineBuilder elevationBuilder;
            if (checked) {
                // The elevated helper registers on our behalf, so the task has to carry our SID;
                // left empty the scheduler would pin it to the admin that approved the prompt.
                elevationBuilder.withRegister()
                    .withLogon()
                    .withDelay(delaySeconds)
                    .withLog(enableLogging)
                    .withForUser(Utils::TaskScheduler::currentUserSid());
            } else {
                elevationBuilder.withUnregister();
            }

            success = Utils::TaskScheduler::runAsAdmin(elevationBuilder.toString());
        }

        return success;
    });
}

} // namespace ModeFlow::Services
