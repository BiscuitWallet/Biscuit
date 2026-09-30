// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ELECTRUMCLIENT_H
#define BISCUIT_ELECTRUMCLIENT_H

#include <functional>

#include <optional>

#include <QHash>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>

#include "Electrum.h"

namespace biscuit::coins {

struct ElectrumServer {
    QString host;
    quint16 port = 0;
    bool tls = true;          // plain TCP only for .onion (Tor encrypts) and local network servers

    bool isOnion() const { return host.endsWith(".onion"); }
    // This computer or the local network (a home node: Umbrel, Start9…).
    bool isLocal() const;
    QString toString() const { return QString("%1:%2").arg(host).arg(port); }

    // "host:port" (TLS), "tls://host:port" or "ssl://host:port", "tcp://host:port"
    // (plain: only .onion and local network), or Electrum's "host:port:s" / ":t".
    static std::optional<ElectrumServer> parse(const QString &text, QString *error = nullptr);
};

// One connection to an Electrum server (JSON-RPC over TLS, one line per message).
//
// Security:
//  - clearnet servers must use TLS; the certificate is accepted if it is valid
//    for a public CA, or pinned on first use (TOFU, like Electrum) and checked
//    on every later connection;
//  - every request times out; a server sending oversized data is dropped.
class ElectrumClient : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(const QJsonValue &result, const QString &error)>;
    // Pinned certificate fingerprints, "host:port" -> sha256 hex. Owned by the caller.
    using PinStore = QHash<QString, QString>;

    explicit ElectrumClient(QObject *parent = nullptr);
    ~ElectrumClient() override;

    void setProxy(const QNetworkProxy &proxy) { m_proxy = proxy; }
    void setPinStore(PinStore *pins) { m_pins = pins; }

    void connectToServer(const ElectrumServer &server);
    void disconnectFromServer();
    bool isReady() const { return m_ready; }
    ElectrumServer server() const { return m_server; }

    void call(const QString &method, const QJsonArray &params, Callback callback);

signals:
    // Emitted once the server answered server.version.
    void ready();
    void failed(const QString &reason);
    void notification(const QString &method, const QJsonArray &params);

private:
    void onEncryptedOrConnected();
    void onReadyRead();
    void fail(const QString &reason);
    bool checkCertificate();

    QSslSocket *m_socket;
    ElectrumServer m_server;
    QNetworkProxy m_proxy{QNetworkProxy::NoProxy};
    PinStore *m_pins = nullptr;
    electrum::LineBuffer m_buffer;
    QHash<int, Callback> m_pending;
    QHash<int, QTimer *> m_timeouts;
    int m_nextId = 1;
    bool m_ready = false;
    bool m_failed = false;
};

}

#endif // BISCUIT_ELECTRUMCLIENT_H
