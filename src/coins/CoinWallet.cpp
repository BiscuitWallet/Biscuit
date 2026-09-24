// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinWallet.h"

#include <memory>

#include <QJsonArray>
#include <QRandomGenerator>
#include <QTimer>

#include "Addresses.h"
#include "ElectrumServers.h"

namespace biscuit::coins {

namespace {
    constexpr quint32 gapLimit = 20;
    const QList<int> feeTargets = {2, 6, 24};   // blocks
}

CoinWallet::CoinWallet(HdAccount account, const QJsonObject &cache, QObject *parent)
    : QObject(parent)
    , m_account(std::move(account))
    , m_client(new ElectrumClient(this))
    , m_scanner(gapLimit)
{
    connect(m_client, &ElectrumClient::ready, this, &CoinWallet::onReady);
    connect(m_client, &ElectrumClient::failed, this, &CoinWallet::onFailed);
    connect(m_client, &ElectrumClient::notification, this, &CoinWallet::onNotification);
    m_client->setPinStore(&m_pins);

    // Cache from the encrypted wallet file: show the last known state at once.
    if (cache.value("version").toInt() == 1) {
        const QJsonObject txs = cache.value("txs").toObject();
        for (auto it = txs.begin(); it != txs.end(); ++it) {
            if (auto parsed = electrum::parseTransaction(it.value().toString()); parsed && parsed->txid == it.key()) {
                m_rawTxs.insert(it.key(), it.value().toString());
                m_parsed.insert(it.key(), *parsed);
            }
        }
        const QJsonObject heights = cache.value("heights").toObject();
        for (auto it = heights.begin(); it != heights.end(); ++it) {
            if (m_rawTxs.contains(it.key())) m_heights.insert(it.key(), it.value().toInt());
        }
        const QJsonObject pins = cache.value("pins").toObject();
        for (auto it = pins.begin(); it != pins.end(); ++it) {
            m_pins.insert(it.key(), it.value().toString());
        }
        const int receiveNext = cache.value("receiveNext").toInt();
        const int changeNext = cache.value("changeNext").toInt();
        for (int i = 0; i < receiveNext + int(gapLimit); ++i) scriptHash({HdAccount::Receive, quint32(i)});
        for (int i = 0; i < changeNext + int(gapLimit); ++i) scriptHash({HdAccount::Change, quint32(i)});
        if (receiveNext > 0) m_scanner.setUsed({HdAccount::Receive, quint32(receiveNext - 1)}, true);
        if (changeNext > 0) m_scanner.setUsed({HdAccount::Change, quint32(changeNext - 1)}, true);
        m_height = cache.value("height").toInt();
        recompute();
    }
}

QString CoinWallet::scriptHash(const electrum::AddressRef &ref) const {
    const QByteArray script = m_account.scriptPubKey(ref.chain, ref.index);
    const QString hash = electrumScriptHash(script);
    auto *self = const_cast<CoinWallet *>(this);
    self->m_scripts.insert(script, ref);
    self->m_scriptHashes.insert(hash, ref);
    return hash;
}

QString CoinWallet::serverName() const {
    return m_client->isReady() ? m_client->server().toString() : QString();
}

void CoinWallet::setProxy(const QNetworkProxy &proxy) {
    m_proxy = proxy;
    if (m_running) {
        connectToServer();
    }
}

void CoinWallet::setCustomServer(const ElectrumServer &server) {
    m_customServer = server;
    if (m_running) {
        connectToServer();
    }
}

void CoinWallet::start() {
    m_running = true;
    connectToServer();
}

void CoinWallet::stop() {
    m_running = false;
    m_client->disconnectFromServer();
    setStatus(Status::Disconnected);
}

void CoinWallet::setStatus(Status status) {
    if (m_status != status) {
        m_status = status;
        emit statusChanged(status);
    }
}

void CoinWallet::connectToServer() {
    ElectrumServer server = m_customServer;
    if (server.host.isEmpty()) {
        const auto servers = defaultElectrumServers(params());
        if (servers.isEmpty()) {
            setStatus(Status::Disconnected);
            return;
        }
        const auto &pick = servers.at(QRandomGenerator::global()->bounded(servers.size()));
        server = {pick.first, pick.second, true};
    }
    setStatus(Status::Connecting);
    m_client->setProxy(m_proxy);
    m_client->connectToServer(server);
}

void CoinWallet::onFailed(const QString &reason) {
    qWarning() << params().ticker << "Electrum:" << reason;
    setStatus(Status::Disconnected);
    if (!m_running) {
        return;
    }
    // Try another server, waiting longer after repeated failures.
    m_failures++;
    const int delayMs = std::min(60, 2 * m_failures) * 1000;
    QTimer::singleShot(delayMs, this, [this] {
        if (m_running && m_status == Status::Disconnected) connectToServer();
    });
}

void CoinWallet::onReady() {
    m_failures = 0;
    setStatus(Status::Synchronizing);
    m_client->call("blockchain.headers.subscribe", {}, [this](const QJsonValue &result, const QString &) {
        const int height = result.toObject().value("height").toInt();
        if (height > 0) m_height = height;
    });
    refreshFees();

    m_scanner = electrum::GapScanner(gapLimit);
    m_heights.clear();
    scanNext();
}

void CoinWallet::scanNext() {
    const auto batch = m_scanner.nextBatch();
    if (batch.isEmpty()) {
        if (m_scanner.done()) {
            fetchMissingTransactions([this] {
                recompute();
                setStatus(Status::Synchronized);
                // Be told when anything changes on our addresses.
                for (quint32 i = 0; i < m_scanner.firstUnused(HdAccount::Receive) + 5; ++i) {
                    m_client->call("blockchain.scripthash.subscribe", {scriptHash({HdAccount::Receive, i})}, nullptr);
                }
                for (quint32 i = 0; i < m_scanner.firstUnused(HdAccount::Change) + 2; ++i) {
                    m_client->call("blockchain.scripthash.subscribe", {scriptHash({HdAccount::Change, i})}, nullptr);
                }
            });
        }
        return;
    }

    m_pendingScans = batch.size();
    for (const auto &ref : batch) {
        m_client->call("blockchain.scripthash.get_history", {scriptHash(ref)},
                       [this, ref](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty()) {
                return;   // the connection failed, onFailed() retries later
            }
            const QJsonArray items = result.toArray();
            for (const QJsonValue &v : items) {
                const QJsonObject item = v.toObject();
                m_heights.insert(item.value("tx_hash").toString(), item.value("height").toInt());
            }
            m_scanner.setUsed(ref, !items.isEmpty());
            if (--m_pendingScans == 0) {
                scanNext();
            }
        });
    }
}

void CoinWallet::fetchMissingTransactions(std::function<void()> then) {
    QStringList missing;
    for (const QString &txid : m_heights.keys()) {
        if (!m_rawTxs.contains(txid)) missing.append(txid);
    }
    if (missing.isEmpty()) {
        then();
        return;
    }
    auto remaining = std::make_shared<int>(missing.size());
    for (const QString &txid : missing) {
        m_client->call("blockchain.transaction.get", {txid}, [this, txid, remaining, then](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty()) {
                return;
            }
            // The txid is recomputed from the data: a server cannot swap transactions.
            const QString hex = result.toString();
            if (auto parsed = electrum::parseTransaction(hex); parsed && parsed->txid == txid) {
                m_rawTxs.insert(txid, hex);
                m_parsed.insert(txid, *parsed);
            }
            if (--*remaining == 0) {
                then();
            }
        });
    }
}

void CoinWallet::refreshFees() {
    for (int target : feeTargets) {
        m_client->call("blockchain.estimatefee", {target}, [this, target](const QJsonValue &result, const QString &error) {
            if (error.isEmpty()) {
                m_feeRates[target] = electrum::feeRateFromEstimate(result.toDouble());
            }
        });
    }
}

void CoinWallet::onNotification(const QString &method, const QJsonArray &params) {
    if (method == QLatin1String("blockchain.headers.subscribe")) {
        const int height = params.first().toObject().value("height").toInt();
        if (height > 0 && height != m_height) {
            m_height = height;
            recompute();   // confirmations changed
            refreshFees();
        }
    } else if (method == QLatin1String("blockchain.scripthash.subscribe") && m_status == Status::Synchronized) {
        // Something happened on one of our addresses: synchronize again.
        onReady();
    }
}

void CoinWallet::recompute() {
    QMap<QString, electrum::ParsedTx> txs;
    for (auto it = m_heights.constBegin(); it != m_heights.constEnd(); ++it) {
        if (m_parsed.contains(it.key())) txs.insert(it.key(), m_parsed.value(it.key()));
    }
    QSet<QByteArray> scripts;
    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) scripts.insert(it.key());

    m_history = electrum::computeHistory(txs, m_heights, scripts);
    m_utxos = electrum::computeUtxos(txs, m_heights, m_scripts);
    emit updated();
    emit cacheChanged();
}

CoinWallet::Balance CoinWallet::balance() const {
    Balance b;
    for (const auto &u : m_utxos) {
        (u.utxo.height > 0 ? b.confirmed : b.unconfirmed) += u.utxo.value;
    }
    return b;
}

QString CoinWallet::receiveAddress() const {
    return m_account.address(HdAccount::Receive, m_scanner.firstUnused(HdAccount::Receive));
}

double CoinWallet::feeRate(int targetBlocks) const {
    // Closest known target at or above the requested one, else 2 sat/vB.
    for (auto it = m_feeRates.lowerBound(targetBlocks); it != m_feeRates.constEnd(); ++it) {
        return it.value();
    }
    return m_feeRates.isEmpty() ? 2.0 : m_feeRates.last();
}

std::optional<TxPlan> CoinWallet::planSend(const QString &address, quint64 amount, double feeRate, bool sendAll,
                                           QString *error) const {
    const auto destination = addressToScriptPubKey(address, params());
    if (!destination) {
        if (error) *error = QString("This is not a valid %1 address.").arg(params().name);
        return std::nullopt;
    }
    // Spend confirmed coins, and unconfirmed change of our own transactions.
    QSet<QString> ownTxs;
    for (const auto &e : m_history) {
        if (e.fee.has_value()) ownTxs.insert(e.txid);
    }
    QList<Utxo> spendable;
    for (const auto &u : m_utxos) {
        if (u.utxo.height > 0 || ownTxs.contains(u.utxo.txid)) spendable.append(u.utxo);
    }
    const QByteArray change = m_account.scriptPubKey(HdAccount::Change, m_scanner.firstUnused(HdAccount::Change));
    return planTransaction(spendable, *destination, amount, feeRate, change, sendAll, error);
}

void CoinWallet::broadcast(const TxPlan &plan, std::function<void(const QString &, const QString &)> callback) {
    QString error;
    const auto tx = signTransaction(plan, m_account, quint32(std::max(0, m_height)), &error);
    if (!tx) {
        callback({}, error);
        return;
    }
    if (!m_client->isReady()) {
        callback({}, "Not connected to a server");
        return;
    }
    m_client->call("blockchain.transaction.broadcast", {tx->hex}, [this, tx = *tx, callback](const QJsonValue &result, const QString &error) {
        if (!error.isEmpty()) {
            callback({}, error);
            return;
        }
        if (result.toString() != tx.txid) {
            callback({}, "The server returned an unexpected transaction id");
            return;
        }
        callback(tx.txid, {});
        onReady();   // show the new transaction
    });
}

QJsonObject CoinWallet::cache() const {
    QJsonObject txs, heights, pins;
    for (auto it = m_heights.constBegin(); it != m_heights.constEnd(); ++it) {
        if (m_rawTxs.contains(it.key())) {
            txs.insert(it.key(), m_rawTxs.value(it.key()));
            heights.insert(it.key(), it.value());
        }
    }
    for (auto it = m_pins.constBegin(); it != m_pins.constEnd(); ++it) {
        pins.insert(it.key(), it.value());
    }
    return {
        {"version", 1},
        {"txs", txs},
        {"heights", heights},
        {"pins", pins},
        {"receiveNext", int(m_scanner.firstUnused(HdAccount::Receive))},
        {"changeNext", int(m_scanner.firstUnused(HdAccount::Change))},
        {"height", m_height},
    };
}

}
