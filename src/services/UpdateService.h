#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ModeFlow::Core {
class ConfigManager;
}

namespace ModeFlow::Services {

class UpdateService : public QObject {
    Q_OBJECT
public:
    explicit UpdateService(Core::ConfigManager* configManager, QObject* parent = nullptr);
    ~UpdateService() override;

    void checkForUpdates(bool force = false);

    bool isUpdateAvailable() const { return m_updateAvailable; }
    bool isCheckingInProgress() const { return m_checkInProgress; }
    QString latestVersion() const { return m_latestVersion; }
    QUrl downloadUrl() const { return m_downloadUrl; }
    QString changelog() const { return m_changelog; }

    static bool isNewerVersion(const QString& remote, const QString& local);

signals:
    void updateAvailable(const QString& version, const QUrl& url, const QString& changelog);
    void noUpdateAvailable();
    void checkFailed(const QString& error);

private slots:
    void onCheckReply(QNetworkReply* reply);

private:
    bool shouldCheck() const;
    void markChecked();
    void loadCachedUpdate();
    void saveUpdateToCache(const QJsonObject&);
    void clearCache();

    QNetworkAccessManager m_network;
    QString m_cacheFilePath;
    bool m_checkInProgress = false;
    bool m_isManualCheck = false;
    bool m_updateAvailable = false;
    QString m_latestVersion;
    QUrl m_downloadUrl;
    QString m_changelog;

    Core::ConfigManager* m_configManager = nullptr;
};

} // namespace ModeFlow::Services
