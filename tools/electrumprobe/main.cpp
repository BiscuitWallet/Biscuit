// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Developer tool: checks Electrum servers with Biscuit's own client.
// Usage: electrumprobe host:port [host:port ...]
// Prints the server version, the network (from the genesis block hash) and
// the fee estimate.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTimer>

#include <algorithm>
#include <cstdio>

#include "ElectrumClient.h"

using namespace biscuit::coins;

namespace {
    QString network(const QString &headerHex) {
        QByteArray h = QCryptographicHash::hash(QCryptographicHash::hash(QByteArray::fromHex(headerHex.toLatin1()),
                                                QCryptographicHash::Sha256), QCryptographicHash::Sha256);
        std::reverse(h.begin(), h.end());
        const QString hash = QString::fromLatin1(h.toHex());
        if (hash == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") return "Bitcoin mainnet";
        if (hash == "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2") return "Litecoin mainnet";
        return "UNKNOWN genesis " + hash;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList servers = app.arguments().mid(1);
    int remaining = servers.size();
    ElectrumClient::PinStore pins;

    for (const QString &s : servers) {
        auto *client = new ElectrumClient(&app);
        client->setPinStore(&pins);
        const ElectrumServer server{s.section(':', 0, 0), quint16(s.section(':', 1, 1).toUInt()), true};
        auto done = [&remaining, &app, client](const QString &line) {
            std::printf("%s\n", qPrintable(line));
            std::fflush(stdout);
            client->disconnect();
            client->deleteLater();
            if (--remaining == 0) app.quit();
        };
        QObject::connect(client, &ElectrumClient::failed, client, [done, s](const QString &reason) {
            done(QString("%1 -> FAILED: %2").arg(s, reason));
        });
        QObject::connect(client, &ElectrumClient::ready, client, [client, done, s] {
            client->call("blockchain.block.header", {0}, [client, done, s](const QJsonValue &header, const QString &err) {
                if (!err.isEmpty()) { done(QString("%1 -> header error: %2").arg(s, err)); return; }
                const QString net = network(header.toString());
                client->call("blockchain.estimatefee", {6}, [done, s, net](const QJsonValue &fee, const QString &) {
                    done(QString("%1 -> OK  %2  fee(6 blocks)=%3/kB").arg(s, net).arg(fee.toDouble()));
                });
            });
        });
        client->connectToServer(server);
    }
    if (remaining == 0) return 0;
    QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    return app.exec();
}
