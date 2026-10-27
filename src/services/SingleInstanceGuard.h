#pragma once

#include <QHash>
#include <QLocalServer>

class QLocalSocket;

namespace ModeFlow::Services {

class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceGuard(QObject* parent = nullptr);
    ~SingleInstanceGuard();

    bool tryToRun();
    void shutdown();

    /// The fixed handshake message both sides build from the current user account.
    static QByteArray activationPayload();

signals:
    void signalRaiseWindow();

private slots:
    bool startAsServer(const QString& serverName);
    void onNewConnection();

private:
    void readActivation(QLocalSocket* client);
    void dropClient(QLocalSocket* client);

    static QString generateUniqueServerName();
    static QString generateSharedSecret();

    QLocalServer* m_server;

    /// Bytes received so far from peers whose handshake message has not arrived completely yet.
    QHash<QLocalSocket*, QByteArray> m_partialPayloads;
};
} // namespace ModeFlow::Services