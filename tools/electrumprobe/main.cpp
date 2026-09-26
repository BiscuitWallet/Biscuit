// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Developer tool: checks Electrum servers with Biscuit's own client.
// Usage: electrumprobe host:port [host:port ...]
// Prints the server version, the network (from the genesis block hash) and
// the fee estimate.
//        electrumprobe address btc|ltc <address> host:port
// Prints the balance and history the server has for one address.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <cstdio>

#include "Addresses.h"
#include "Bip39.h"
#include "Electrum.h"
#include "CoinWallet.h"
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

int syncMode(QCoreApplication &app, const QString &coin, const QString &mnemonic) {
    // Read-only sync of a wallet, to test CoinWallet against real servers.
    const auto &params = coin == "ltc" ? litecoin() : bitcoin();
    auto account = HdAccount::fromSeed(*bip39::mnemonicToSeed(mnemonic), params);
    auto *wallet = new CoinWallet(std::move(*account), {}, &app);
    QObject::connect(wallet, &CoinWallet::statusChanged, [wallet, &app](CoinWallet::Status st) {
        std::printf("status: %d server: %s\n", int(st), qPrintable(wallet->serverName()));
        if (st == CoinWallet::Status::Synchronized) {
            const auto b = wallet->balance();
            std::printf("height %d, %lld transactions, balance %llu sats confirmed + %llu unconfirmed\n",
                        wallet->blockHeight(), (long long)wallet->history().size(), b.confirmed, b.unconfirmed);
            std::printf("next receive address: %s\n", qPrintable(wallet->receiveAddress()));
            int shown = 0;
            for (const auto &e : wallet->history()) {
                if (shown++ == 3) break;
                std::printf("  %s height %d delta %lld\n", qPrintable(e.txid), e.height, (long long)e.delta);
            }
            std::printf("fee rates: %.1f / %.1f sat/vB\n", wallet->feeRate(2), wallet->feeRate(24));
            std::fflush(stdout);
            app.quit();
        }
    });
    wallet->start();
    QTimer::singleShot(120000, &app, &QCoreApplication::quit);
    return app.exec();
}

int addressMode(QCoreApplication &app, const QString &coin, const QString &address, const QString &hostPort) {
    const auto &params = coin == "ltc" ? litecoin() : bitcoin();
    const auto script = addressToScriptPubKey(address, params);
    if (!script) {
        std::printf("not a valid %s address\n", qPrintable(params.name));
        return 1;
    }
    const QString hash = electrumScriptHash(*script);
    ElectrumClient::PinStore pins;
    auto *client = new ElectrumClient(&app);
    client->setPinStore(&pins);
    QObject::connect(client, &ElectrumClient::failed, &app, [&app](const QString &reason) {
        std::printf("FAILED: %s\n", qPrintable(reason));
        app.quit();
    });
    QObject::connect(client, &ElectrumClient::ready, client, [client, hash, &app] {
        client->call("blockchain.scripthash.get_balance", {hash}, [client, hash, &app](const QJsonValue &b, const QString &err) {
            std::printf("balance: %s\n", err.isEmpty() ? qPrintable(QString::fromUtf8(QJsonDocument(b.toObject()).toJson(QJsonDocument::Compact)))
                                                      : qPrintable(err));
            client->call("blockchain.scripthash.get_history", {hash}, [client, &app](const QJsonValue &h, const QString &err2) {
                std::printf("history: %s\n", err2.isEmpty() ? qPrintable(QString::fromUtf8(QJsonDocument(h.toArray()).toJson(QJsonDocument::Compact)))
                                                           : qPrintable(err2));
                const QString txid = h.toArray().first().toObject().value("tx_hash").toString();
                if (txid.isEmpty()) { app.quit(); return; }
                // Same check as the wallet: does Biscuit's parser read this transaction?
                client->call("blockchain.transaction.get", {txid}, [txid, &app](const QJsonValue &raw, const QString &err3) {
                    const QString hex = raw.toString();
                    std::printf("raw tx (%lld bytes): %s\n", (long long)hex.size() / 2, err3.isEmpty() ? qPrintable(hex.left(240)) : qPrintable(err3));
                    const auto parsed = electrum::parseTransaction(hex);
                    if (!parsed) std::printf("PARSE FAILED\n");
                    else std::printf("parsed txid %s (%s), %lld inputs, %lld outputs\n", qPrintable(parsed->txid),
                                     parsed->txid == txid ? "matches" : "MISMATCH", (long long)parsed->inputs.size(), (long long)parsed->outputs.size());
                    std::fflush(stdout);
                    app.quit();
                });
            });
        });
    });
    client->connectToServer({hostPort.section(':', 0, 0), quint16(hostPort.section(':', 1, 1).toUInt()), true});
    QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    return app.exec();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "address") {
        return addressMode(app, app.arguments().value(2), app.arguments().value(3), app.arguments().value(4));
    }
    if (app.arguments().value(1) == "sync") {
        return syncMode(app, app.arguments().value(2), app.arguments().value(3));
    }
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
