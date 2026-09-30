// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinWallet.h"

#include <memory>

#include <QJsonArray>
#include <QRandomGenerator>
#include <QTimer>

#include "Addresses.h"
#include "ElectrumServers.h"
#include "SilentPayments.h"

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
        for (const QJsonValue &key : cache.value("frozen").toArray()) {
            m_frozen.insert(key.toString());
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
        const QJsonObject times = cache.value("blockTimes").toObject();
        for (auto it = times.begin(); it != times.end(); ++it) {
            m_blockTimes.insert(it.key().toInt(), qint64(it.value().toDouble()));
        }
        const QJsonObject seen = cache.value("firstSeen").toObject();
        for (auto it = seen.begin(); it != seen.end(); ++it) {
            m_firstSeen.insert(it.key(), qint64(it.value().toDouble()));
        }
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

QStringList CoinWallet::electrumUrls() const {
    auto url = [](const QString &host, quint16 port, bool tls) {
        return QString("%1://%2:%3").arg(tls ? "ssl" : "tcp", host).arg(port);
    };
    if (!m_customServer.host.isEmpty()) {
        return {url(m_customServer.host, m_customServer.port, m_customServer.tls)};
    }
    QStringList urls;
    for (const auto &server : defaultElectrumServers(params())) {
        urls << url(server.first, server.second, true);
    }
    return urls;
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

// A request answered with an error by a connected server would leave the
// synchronization unfinished: drop that server and synchronize with another.
// (When the connection itself failed, onFailed() already handles it.)
void CoinWallet::onRequestError(const QString &error) {
    if (!m_client->isReady()) {
        return;
    }
    m_client->disconnectFromServer();
    onFailed("Server error: " + error);
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
            fetchMissingTransactions([this] { fetchBlockTimes([this] {
                recompute();
                setStatus(Status::Synchronized);
                // Be told when anything changes on our addresses.
                for (quint32 i = 0; i < m_scanner.firstUnused(HdAccount::Receive) + 5; ++i) {
                    m_client->call("blockchain.scripthash.subscribe", {scriptHash({HdAccount::Receive, i})}, nullptr);
                }
                for (quint32 i = 0; i < m_scanner.firstUnused(HdAccount::Change) + 2; ++i) {
                    m_client->call("blockchain.scripthash.subscribe", {scriptHash({HdAccount::Change, i})}, nullptr);
                }
            }); });
        }
        return;
    }

    m_pendingScans = batch.size();
    for (const auto &ref : batch) {
        m_client->call("blockchain.scripthash.get_history", {scriptHash(ref)},
                       [this, ref](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty()) {
                onRequestError(error);
                return;
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
                onRequestError(error);
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

void CoinWallet::fetchBlockTimes(std::function<void()> then) {
    QList<int> missing;
    for (int height : m_heights) {
        if (height > 0 && !m_blockTimes.contains(height) && !missing.contains(height)) missing.append(height);
    }
    if (missing.isEmpty()) {
        then();
        return;
    }
    auto remaining = std::make_shared<int>(missing.size());
    for (int height : missing) {
        m_client->call("blockchain.block.header", {height}, [this, height, remaining, then](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty()) {
                onRequestError(error);
                return;
            }
            // 80-byte header; the time is the little-endian uint32 at offset 68.
            const QByteArray header = QByteArray::fromHex(result.toString().toLatin1());
            if (header.size() == 80) {
                const auto *t = reinterpret_cast<const uchar *>(header.constData() + 68);
                m_blockTimes.insert(height, qint64(t[0]) | qint64(t[1]) << 8 | qint64(t[2]) << 16 | qint64(t[3]) << 24);
            }
            if (--*remaining == 0) {
                then();
            }
        });
    }
}

QDateTime CoinWallet::transactionTime(const QString &txid) const {
    const int height = m_heights.value(txid, 0);
    if (height > 0 && m_blockTimes.contains(height)) {
        return QDateTime::fromSecsSinceEpoch(m_blockTimes.value(height));
    }
    if (m_firstSeen.contains(txid)) {
        return QDateTime::fromSecsSinceEpoch(m_firstSeen.value(txid));
    }
    return {};
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
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto &e : m_history) {
        if (!m_firstSeen.contains(e.txid)) m_firstSeen.insert(e.txid, now);
    }
    m_utxos = electrum::computeUtxos(txs, m_heights, m_scripts);
    emit updated();
    emit cacheChanged();
}

void CoinWallet::setFrozen(const QStringList &coinKeys, bool frozen) {
    for (const QString &key : coinKeys) {
        if (frozen) m_frozen.insert(key); else m_frozen.remove(key);
    }
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
                                           QString *error, const QStringList &onlyCoins) const {
    // Silent payment: plan with a Taproot output of the right size, then
    // derive the real one from the coins the plan spends.
    const auto silent = sp::addressFor(address, params());
    const auto destination = silent ? std::optional<QByteArray>(sp::taprootScript(QByteArray(32, '\0')))
                                    : addressToScriptPubKey(address, params());
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
        const bool chosen = onlyCoins.isEmpty() ? !isFrozen(u.utxo) : onlyCoins.contains(coinKey(u.utxo));
        if (chosen && (u.utxo.height > 0 || ownTxs.contains(u.utxo.txid))) spendable.append(u.utxo);
    }
    if (spendable.isEmpty() && !onlyCoins.isEmpty()) {
        if (error) *error = "The selected coins are no longer available, or not confirmed yet.";
        return std::nullopt;
    }
    const QByteArray change = m_account.scriptPubKey(HdAccount::Change, m_scanner.firstUnused(HdAccount::Change));
    auto plan = planTransaction(spendable, *destination, amount, feeRate, change, sendAll, error);
    if (!plan || !silent) {
        return plan;
    }

    // Our coins are all P2WPKH: every input counts toward the shared secret.
    QList<sp::Input> inputs;
    for (const Utxo &u : plan->inputs) {
        inputs.append({u.txid, u.vout, m_account.privateKey(u.chain, u.index), false, true});
    }
    const auto keys = sp::outputKeys(inputs, {*silent}, error);
    if (!keys) {
        return std::nullopt;
    }
    for (int i = 0; i < plan->outputs.size(); ++i) {
        if (i != plan->changeOutput) {
            plan->outputs[i].scriptPubKey = sp::taprootScript(keys->first());
        }
    }
    plan->silentPaymentAddress = address.trimmed();
    return plan;
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
        {"frozen", [this] {
            QJsonArray a;
            for (const auto &u : m_utxos) {
                if (isFrozen(u.utxo)) a.append(coinKey(u.utxo));   // spent coins drop out
            }
            return a;
        }()},
        {"receiveNext", int(m_scanner.firstUnused(HdAccount::Receive))},
        {"changeNext", int(m_scanner.firstUnused(HdAccount::Change))},
        {"height", m_height},
        {"blockTimes", [this] {
            QJsonObject o;
            for (auto it = m_blockTimes.constBegin(); it != m_blockTimes.constEnd(); ++it) o.insert(QString::number(it.key()), double(it.value()));
            return o;
        }()},
        {"firstSeen", [this, &txs] {
            QJsonObject o;
            for (auto it = m_firstSeen.constBegin(); it != m_firstSeen.constEnd(); ++it) {
                if (txs.contains(it.key())) o.insert(it.key(), double(it.value()));
            }
            return o;
        }()},
    };
}

}
