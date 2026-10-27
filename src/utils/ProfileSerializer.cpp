#include "ProfileSerializer.h"

#include <QFile>
#include <QJsonArray>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace ModeFlow::Utils {

bool ProfileSerializer::exportProfiles(const QList<Core::WorkspaceConfig>& profiles, const QString& filePath) {
    QJsonObject root;
    root[u"version"_s] = 1;
    root[u"app"_s] = u"ModeFlow"_s;

    QJsonArray profilesArray;
    for (const auto& profile : profiles) {
        profilesArray.append(profile.toJson());
    }
    root[u"profiles"_s] = profilesArray;

    QJsonDocument doc(root);
    const QByteArray json = doc.toJson(QJsonDocument::Indented);

    // A plain QFile truncates the previous export before writing, so a failed or partial write leaves
    // the user with nothing. QSaveFile writes a sibling temporary file and only replaces the target
    // once the whole payload is committed.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    if (file.write(json) != json.size()) {
        return false;
    }

    return file.commit();
}

QList<Core::WorkspaceConfig> ProfileSerializer::importProfiles(const QString& filePath, QString& errorOut) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorOut = u"Cannot open file: %1"_s.arg(filePath);
        return {};
    }

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        errorOut = u"JSON parse error: %1"_s.arg(parseError.errorString());
        return {};
    }

    if (!doc.isObject()) {
        errorOut = u"Invalid JSON format: expected object"_s;
        return {};
    }

    QJsonObject root = doc.object();

    if (root[u"app"_s].toString() != u"ModeFlow"_s) {
        errorOut = u"Invalid file: not a ModeFlow profile export"_s;
        return {};
    }

    int version = root[u"version"_s].toInt();
    if (version != 1) {
        errorOut = u"Unsupported version: %1"_s.arg(version);
        return {};
    }

    QJsonArray profilesArray = root[u"profiles"_s].toArray();
    QList<Core::WorkspaceConfig> profiles;

    for (const auto& value : profilesArray) {
        if (!value.isObject())
            continue;

        Core::WorkspaceConfig cfg = Core::WorkspaceConfig::fromJson(value.toObject());
        profiles.append(cfg);
    }

    return profiles;
}

bool ProfileSerializer::isValidProfileFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
        return false;

    if (!doc.isObject())
        return false;

    QJsonObject root = doc.object();
    return root[u"app"_s].toString() == u"ModeFlow"_s && root.contains(u"profiles"_s);
}

} // namespace ModeFlow::Utils
