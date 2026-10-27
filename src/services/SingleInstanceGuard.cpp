#include "SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>
#include <QTimer>

#include "Constants.h"
#include "Logging.h"
#include "VersionInfo.h"

namespace ModeFlow::Services {

using namespace Qt::StringLiterals;

SingleInstanceGuard::SingleInstanceGuard(QObject* parent) : QObject(parent), m_server(nullptr) {}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
    }
}

QString SingleInstanceGuard::generateUniqueServerName() {
    const QString appIdentifier = u"%1::%2"_s.arg(APP_COMPANY_NAME, APP_PRODUCT_NAME);
    QString serverName = QCryptographicHash::hash(appIdentifier.toUtf8(), QCryptographicHash::Sha256).toHex();
    serverName.prepend("ModeFlow_");
    return serverName;
}

QString SingleInstanceGuard::generateSharedSecret() {
    const QString salt = u"ModeFlow_IPC_Secret_2026_Salt_q89wery"_s;
    const QString userName = qEnvironmentVariable("USERNAME");
    return QCryptographicHash::hash((userName + salt).toUtf8(), QCryptographicHash::Sha256).toHex();
}

QByteArray SingleInstanceGuard::activationPayload() {
    return "ACTIVATE:" + generateSharedSecret().toUtf8();
}

bool SingleInstanceGuard::tryToRun() {
    const QString serverName = generateUniqueServerName();

    QLocalSocket socket;
    socket.connectToServer(serverName);

    if (socket.waitForConnected(Utils::LocalSocketTimeoutMs)) {
        socket.write(activationPayload());
        socket.waitForBytesWritten(Utils::LocalSocketTimeoutMs);

        return false;
    }

    return startAsServer(serverName);
}

bool SingleInstanceGuard::startAsServer(const QString& serverName) {
    auto* server = new QLocalServer(this);

    if (!server->listen(serverName)) {
        if (server->serverError() == QAbstractSocket::AddressInUseError) {
            QLocalServer::removeServer(serverName);
            if (!server->listen(serverName)) {
                qCCritical(lcService) << "Critical error: failed to listen after clearing dead socket:"
                                      << server->errorString();
                delete server;
                return false;
            }
        } else {
            qCCritical(lcService) << "Local server listen failed:" << server->errorString();
            delete server;
            return false;
        }
    }

    m_server = server;
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::onNewConnection);

    return true;
}

void SingleInstanceGuard::onNewConnection() {
    while (QLocalSocket* client = m_server->nextPendingConnection()) {
        m_partialPayloads.insert(client, QByteArray());

        // The GUI thread must not block on a peer that may never send anything, so a silent
        // client is dropped through the event loop instead of waitForReadyRead().
        auto* deadline = new QTimer(client);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, client, [client]() { client->close(); });
        deadline->start(Utils::LocalSocketTimeoutMs);

        connect(client, &QLocalSocket::readyRead, this, [this, client]() { readActivation(client); });
        connect(client, &QLocalSocket::disconnected, this, [this, client]() {
            m_partialPayloads.remove(client);
            client->deleteLater();
        });
    }
}

void SingleInstanceGuard::readActivation(QLocalSocket* client) {
    const auto pending = m_partialPayloads.find(client);
    if (pending == m_partialPayloads.end()) {
        return;
    }
    pending->append(client->readAll());

    const QByteArray expected = activationPayload();

    // Anything that is not a prefix of the fixed message can never become valid, and the
    // buffer therefore cannot grow beyond the message itself.
    if (!expected.startsWith(pending.value())) {
        qCWarning(lcService) << "Blocked unauthorized local socket connection!";
        dropClient(client);
        return;
    }

    if (pending.value() == expected) {
        dropClient(client);
        emit signalRaiseWindow();
    }
}

void SingleInstanceGuard::dropClient(QLocalSocket* client) {
    m_partialPayloads.remove(client);
    client->close();
    client->deleteLater();
}

void SingleInstanceGuard::shutdown() {
    if (m_server) {
        m_server->close();
        qCDebug(lcService) << "Server closed for restart.";
    }
}

} // namespace ModeFlow::Services