#pragma once

#include <QCoreApplication>

#include "CliArgs.h"

namespace ModeFlow::Core {

class CliParser {
public:
    enum class RunMode { NormalGui, RegisterTask, UnregisterTask };
    enum class Outcome { Run, ShowHelp, ShowVersion, Failed };

    struct Result {
        Outcome outcome = Outcome::Run;
        RunMode mode = RunMode::NormalGui;
        StartupOptions options;

        /// Help or version text for ShowHelp/ShowVersion, reason for Failed.
        QString text;
    };

    static Result parse(const QStringList& arguments);
};

} // namespace ModeFlow::Core
