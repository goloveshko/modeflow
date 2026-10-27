#include "ConfigTypes.h"

#include <QJsonArray>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace ModeFlow::Core {

QKeySequence singleChord(const QKeySequence& sequence) {
    if (sequence.count() <= 1)
        return sequence;
    return QKeySequence(sequence[0]);
}

QJsonObject AppLaunchConfig::toJson() const {
    QJsonObject obj;
    obj[u"appPath"_s] = appPath;
    obj[u"delaySeconds"_s] = delaySeconds;
    obj[u"closeOnExit"_s] = closeOnExit;
    return obj;
}

AppLaunchConfig AppLaunchConfig::fromJson(const QJsonObject& obj) {
    AppLaunchConfig cfg;
    cfg.appPath = obj[u"appPath"_s].toString();
    cfg.delaySeconds = obj[u"delaySeconds"_s].toInt(0);
    cfg.closeOnExit = obj[u"closeOnExit"_s].toBool(false);
    cfg.delaySeconds = std::clamp(cfg.delaySeconds, 0, 300);
    return cfg;
}

QJsonObject WorkspaceConfig::toJson() const {
    QJsonObject obj;
    obj[u"id"_s] = id;
    obj[u"name"_s] = name;
    obj[u"iconSymbol"_s] = iconSymbol;
    obj[u"hotkey"_s] = hotkey.toString(QKeySequence::PortableText);
    obj[u"displayId"_s] = displayId;
    obj[u"audioId"_s] = audioId;
    obj[u"skipInCycle"_s] = skipInCycle;

    QJsonArray appsArray;
    for (const auto& app : appsToLaunch) {
        appsArray.append(app.toJson());
    }
    obj[u"appsToLaunch"_s] = appsArray;

    return obj;
}

WorkspaceConfig WorkspaceConfig::fromJson(const QJsonObject& obj) {
    WorkspaceConfig cfg;
    cfg.id = obj[u"id"_s].toString();
    cfg.name = obj[u"name"_s].toString(u"Workspace"_s);
    cfg.iconSymbol = obj[u"iconSymbol"_s].toString();
    cfg.hotkey = singleChord(QKeySequence(obj[u"hotkey"_s].toString(), QKeySequence::PortableText));
    cfg.displayId = obj[u"displayId"_s].toString();
    cfg.audioId = obj[u"audioId"_s].toString();
    cfg.skipInCycle = obj[u"skipInCycle"_s].toBool(false);

    const QJsonArray appsArray = obj[u"appsToLaunch"_s].toArray();
    for (const auto& val : appsArray) {
        cfg.appsToLaunch.append(AppLaunchConfig::fromJson(val.toObject()));
    }

    // Migrate legacy single-app fields for backward compatibility
    if (cfg.appsToLaunch.isEmpty()) {
        const bool legacyRunApp = obj[u"runApp"_s].toBool(false);
        const QString legacyPath = obj[u"appPath"_s].toString();
        const int legacyDelay = obj[u"delaySeconds"_s].toInt(3);

        if (legacyRunApp && !legacyPath.isEmpty()) {
            AppLaunchConfig legacy;
            legacy.appPath = legacyPath;
            legacy.delaySeconds = std::clamp(legacyDelay, 0, 300);
            cfg.appsToLaunch.append(legacy);
        }
    }

    if (cfg.id.isEmpty())
        cfg.id = QUuid::createUuid().toString();

    return cfg;
}

} // namespace ModeFlow::Core
