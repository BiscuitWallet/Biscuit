// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ETHWALLET_H
#define BISCUIT_ETHWALLET_H

#include <functional>
#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include "core/Eth.h"
#include "core/EthApi.h"

namespace biscuit::coins {

// The Ethereum wallet: ETH, USDT and USDC on one address, from the BIP39
// seed (m/44'/60'/0'/0/0). Balance, nonce and fees come from public nodes
// (one at a time, the next one if it fails); the history from Blockscout,
// which never decides anything about money. Keeps a cache (balances,
// history) that the owner stores in the encrypted wallet file.
class EthWallet : public QObject {
    Q_OBJECT

public:
    enum class Status { Disconnected, Connecting, Synchronized };
    Q_ENUM(Status)

    EthWallet(eth::Account account, const QJsonObject &cache, QObject *parent = nullptr);

    QByteArray address() const { return m_address; }
    QString addressText() const;   // 0x… with its checksum
    Status status() const { return m_status; }
    QString nodeName() const;

    // "ETH", "USDT", "USDC": the assets of this wallet, and their decimals.
    static QStringList assets();
    static int decimals(const QString &asset);
    eth::u128 balance(const QString &asset) const { return m_balances.value(asset); }
    // Newest first, pending ones on top.
    QList<eth::HistoryEntry> history() const;
    std::optional<eth::Fees> fees() const { return m_fees; }

    // Empty = the built-in nodes / explorer.
    void setCustomNode(const QString &url);
    void setCustomBlockscout(const QString &url);
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    void refresh();

    // A send, checked and priced, waiting for the user's confirmation.
    struct Plan {
        QString asset;
        QByteArray recipient;   // who gets the asset (not the token contract)
        eth::u128 amount = 0;
        eth::u128 maxFee = 0;   // gas limit x max fee per gas: the most it can cost
        eth::Transaction tx;
    };
    // `amountText` is a decimal amount or "all". Asks the node for the nonce
    // and the gas, so the answer comes later.
    void planSend(const QString &asset, const QString &address, const QString &amountText,
                  std::function<void(std::optional<Plan> plan, const QString &error)> done);
    // Signs, reads the signed bytes back to make sure they say exactly what
    // the plan says, and sends them to every node.
    void broadcast(const Plan &plan, std::function<void(const QString &txHash, const QString &error)> done);

    QJsonObject cache() const;

signals:
    void statusChanged(Status status);
    void updated();          // balances, fees or history changed
    void cacheChanged();     // cache() should be saved

private:
    using RpcDone = std::function<void(std::optional<QJsonValue> result, const QString &error)>;
    // On the current node; the next ones in turn if it does not answer.
    void call(const QString &method, const QJsonArray &params, RpcDone done, int attempt = 0);
    void fetchJson(const QString &url, std::function<void(std::optional<QJsonValue>)> done);
    QStringList nodes() const;
    QString blockscout() const;
    bool networkAllowed() const;
    void setStatus(Status status);
    void refreshNode();
    void refreshHistory();
    void mergeHistory(const QList<eth::HistoryEntry> &entries, const QHash<QByteArray, eth::u128> &fees);

    eth::Account m_account;
    QByteArray m_address;
    QString m_customNode;
    QString m_customBlockscout;
    int m_node = 0;               // index in nodes() currently used
    int m_failures = 0;           // nodes tried in a row without success
    bool m_running = false;
    Status m_status = Status::Disconnected;
    QTimer m_timer;
    quint64 m_generation = 0;     // replies of an older refresh are dropped

    QHash<QString, eth::u128> m_balances;
    std::optional<eth::Fees> m_fees;
    QHash<QString, eth::HistoryEntry> m_history;   // key: hash + asset
    QHash<QString, eth::HistoryEntry> m_sent;      // broadcast here, not seen by Blockscout yet
};

}

#endif // BISCUIT_ETHWALLET_H
