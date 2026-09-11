#include "UpdateService.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVersionNumber>

#include "ConfigManager.h"
#include "Constants.h"
#include "Logging.h"
#include "VersionInfo.h"

namespace ModeFlow::Services {

using namespace Qt::StringLiterals;

namespace {
QString sanitizeVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(u'v', Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}
} // namespace

UpdateService::UpdateService(Core::ConfigManager* configManager, QObject* parent)
    : QObject(parent), m_configManager(configManager) {
    m_network.setTransferTimeout(15000);

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_cacheFilePath = QDir::toNativeSeparators(cacheDir + u"/update_cache.json"_s);

    QDir().mkpath(cacheDir);

    loadCachedUpdate();
}

UpdateService::~UpdateService() = default;

void UpdateService::checkForUpdates(bool force) {
    if (m_checkInProgress) {
        return;
    }

    m_isManualCheck = force;

    if (!force && !shouldCheck()) {
        qCDebug(lcService) << "Update check skipped (last check was <24h ago)";
        if (m_updateAvailable && m_configManager && m_latestVersion != m_configManager->skippedVersion()) {
            emit updateAvailable(m_latestVersion, m_downloadUrl, m_changelog);
        } else {
            emit noUpdateAvailable();
        }
        return;
    }

    m_checkInProgress = true;

    const QUrl manifestUrl = QUrl::fromUserInput(Info::UpdateManifestUrl);
    QNetworkRequest request(manifestUrl);
    request.setRawHeader("User-Agent", APP_INTERNAL_NAME " UpdateChecker");
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onCheckReply(reply); });
}

void UpdateService::onCheckReply(QNetworkReply* reply) {
    if (!reply) {
        return;
    }

    disconnect(reply, &QNetworkReply::finished, this, nullptr);

    m_checkInProgress = false;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcService) << "Update check failed:" << reply->errorString();
        emit checkFailed(reply->errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcService) << "Failed to parse update manifest:" << parseError.errorString();
        emit checkFailed(parseError.errorString());
        return;
    }

    const QJsonObject manifest = doc.object();

    const QString latestVersion = sanitizeVersion(manifest[u"tag_name"_s].toString());
    const QUrl downloadUrl(manifest[u"html_url"_s].toString());
    const QString changelog = manifest[u"body"_s].toString();

    if (latestVersion.isEmpty() || downloadUrl.isEmpty()) {
        qCWarning(lcService) << "Update manifest is missing required fields (tag_name/version or html_url/url)";
        emit checkFailed(tr("Invalid update manifest"));
        return;
    }

    markChecked();

    if (!isNewerVersion(latestVersion, Info::Version)) {
        qCDebug(lcService) << "Already up to date:" << Info::Version;
        clearCache();
        emit noUpdateAvailable();
        return;
    }

    qCDebug(lcService) << "Update available:" << latestVersion;
    m_updateAvailable = true;
    m_latestVersion = latestVersion;
    m_downloadUrl = downloadUrl;
    m_changelog = changelog;

    saveUpdateToCache(manifest);

    // Suppress update notification on automatic check if user chose to skip this specific version
    if (!m_isManualCheck && m_configManager && m_configManager->skippedVersion() == latestVersion) {
        qCDebug(lcService) << "Update" << latestVersion << "was skipped by user preference.";
        emit noUpdateAvailable();
        return;
    }

    emit updateAvailable(latestVersion, downloadUrl, changelog);
}

bool UpdateService::isNewerVersion(const QString& remote, const QString& local) const {
    const auto remoteVer = QVersionNumber::fromString(sanitizeVersion(remote));
    const auto localVer = QVersionNumber::fromString(sanitizeVersion(local));

    return remoteVer > localVer;
}

bool UpdateService::shouldCheck() const {
    if (!m_configManager) {
        return true;
    }
    const qint64 last = m_configManager->lastUpdateCheckTimestamp();
    return QDateTime::currentMSecsSinceEpoch() - last > Utils::UpdateCheckIntervalMs;
}

void UpdateService::markChecked() {
    if (m_configManager) {
        m_configManager->setLastUpdateCheckTimestamp(QDateTime::currentMSecsSinceEpoch());
        m_configManager->saveConfig();
    }
}

void UpdateService::loadCachedUpdate() {
    QFile file(m_cacheFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        clearCache();
        return;
    }

    const QJsonObject manifest = doc.object();

    const QString version = sanitizeVersion(manifest[u"tag_name"_s].toString());
    const QString url = manifest[u"html_url"_s].toString();
    const QString changelog = manifest[u"body"_s].toString();

    if (!isNewerVersion(version, Info::Version)) {
        clearCache();
        return;
    }

    m_updateAvailable = true;
    m_latestVersion = version;
    m_downloadUrl = QUrl(url);
    m_changelog = changelog;
    qCDebug(lcService) << "Loaded cached update from file:" << version;
}

void UpdateService::saveUpdateToCache(const QJsonObject& manifest) {
    const QFileInfo fi(m_cacheFilePath);
    QDir().mkpath(fi.absolutePath());

    QFile file(m_cacheFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonDocument doc(manifest);
        file.write(doc.toJson(QJsonDocument::Compact));
        file.close();
        qCDebug(lcService) << "Saved update manifest to cache file:" << m_latestVersion;
    } else {
        qCWarning(lcService) << "Failed to write update cache file:" << m_cacheFilePath;
    }
}

void UpdateService::clearCache() {
    m_updateAvailable = false;
    m_latestVersion.clear();
    m_downloadUrl.clear();
    m_changelog.clear();

    if (QFile::exists(m_cacheFilePath)) {
        QFile::remove(m_cacheFilePath);
        qCDebug(lcService) << "Cleared stale update cache file.";
    }
}

} // namespace ModeFlow::Services