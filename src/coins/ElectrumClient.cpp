// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "ElectrumClient.h"

#include <QCryptographicHash>
#include <QSslCertificate>

namespace biscuit::coins {

namespace {
    constexpr int connectTimeoutMs = 30 * 1000;
    constexpr int requestTimeoutMs = 30 * 1000;
    constexpr char clientName[] = "Biscuit";   // same for every user
    constexpr char protocolVersion[] = "1.4";
}

ElectrumClient::ElectrumClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QSslSocket(this))
{
    connect(m_socket, &QSslSocket::encrypted, this, &ElectrumClient::onEncryptedOrConnected);
    connect(m_socket, &QSslSocket::connected, this, [this] {
        if (!m_server.tls) onEncryptedOrConnected();
    });
    connect(m_socket, &QSslSocket::readyRead, this, &ElectrumClient::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, [this] { fail("Disconnected"); });
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        fail(m_socket->errorString());
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) {
        // Not signed by a public CA: accept only if it matches the pinned
        // certificate, or pin it on first use.
        if (checkCertificate()) {
            m_socket->ignoreSslErrors();
        }
    });
}

ElectrumClient::~ElectrumClient() {
    m_failed = true;   // no callbacks during destruction
    m_socket->abort();
}

void ElectrumClient::connectToServer(const ElectrumServer &server) {
    disconnectFromServer();
    m_server = server;
    m_failed = false;
    m_ready = false;
    m_buffer.clear();

    if (!server.tls && !server.isOnion()) {
        fail("Refusing an unencrypted connection to a clearnet server");
        return;
    }

    m_socket->setProxy(m_proxy);
    QTimer::singleShot(connectTimeoutMs, this, [this, host = server.host] {
        if (!m_ready && !m_failed && m_server.host == host) fail("Connection timed out");
    });

    if (server.tls) {
        m_socket->setPeerVerifyName(server.host);
        m_socket->connectToHostEncrypted(server.host, server.port);
    } else {
        m_socket->connectToHost(server.host, server.port);
    }
}

void ElectrumClient::disconnectFromServer() {
    const bool wasFailed = m_failed;
    m_failed = true;
    m_socket->abort();
    m_failed = wasFailed;
    m_ready = false;
    for (QTimer *t : std::as_const(m_timeouts)) t->deleteLater();
    m_timeouts.clear();
    m_pending.clear();
}

bool ElectrumClient::checkCertificate() {
    const QSslCertificate cert = m_socket->peerCertificate();
    if (cert.isNull() || !m_pins) {
        return false;
    }
    const QString fingerprint = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
    const QString key = m_server.toString();
    const auto pinned = m_pins->constFind(key);
    if (pinned == m_pins->constEnd()) {
        m_pins->insert(key, fingerprint);   // trust on first use
        return true;
    }
    return *pinned == fingerprint;
}

void ElectrumClient::onEncryptedOrConnected() {
    call("server.version", {clientName, protocolVersion}, [this](const QJsonValue &, const QString &error) {
        if (!error.isEmpty()) {
            fail("Server refused protocol version: " + error);
            return;
        }
        m_ready = true;
        emit ready();
    });
}

void ElectrumClient::call(const QString &method, const QJsonArray &params, Callback callback) {
    if (m_failed || m_socket->state() != QAbstractSocket::ConnectedState) {
        if (callback) callback({}, "Not connected");
        return;
    }
    const int id = m_nextId++;
    m_pending.insert(id, std::move(callback));

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        fail(QString("Request %1 timed out").arg(id));
    });
    timer->start(requestTimeoutMs);
    m_timeouts.insert(id, timer);

    m_socket->write(electrum::request(id, method, params));
}

void ElectrumClient::onReadyRead() {
    const auto lines = m_buffer.feed(m_socket->readAll());
    if (!lines) {
        fail("Server sent oversized data");
        return;
    }
    for (const QByteArray &line : *lines) {
        const auto msg = electrum::parseMessage(line);
        if (!msg) {
            continue;
        }
        if (!msg->id) {
            emit notification(msg->method, msg->params);
            continue;
        }
        if (QTimer *t = m_timeouts.take(*msg->id)) {
            t->deleteLater();
        }
        const Callback cb = m_pending.take(*msg->id);
        if (cb) {
            cb(msg->result, msg->error);
        }
        if (m_failed) {
            return;   // a callback disconnected us
        }
    }
}

void ElectrumClient::fail(const QString &reason) {
    if (m_failed) {
        return;
    }
    m_failed = true;
    m_ready = false;
    const auto pending = std::exchange(m_pending, {});
    for (QTimer *t : std::as_const(m_timeouts)) t->deleteLater();
    m_timeouts.clear();
    m_socket->abort();
    for (const Callback &cb : pending) {
        if (cb) cb({}, reason);
    }
    emit failed(reason);
}

}
