// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINWALLET_H
#define BISCUIT_COINWALLET_H

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>

#include "Electrum.h"
#include "ElectrumClient.h"
#include "HdAccount.h"
#include "Transactions.h"

namespace biscuit::coins {

// A light BTC or LTC wallet: keys from the BIP39 seed, data from Electrum
// servers. Keeps a cache (transactions, used addresses, pinned certificates)
// that the owner stores in the encrypted wallet file.
class CoinWallet : public QObject {
    Q_OBJECT

public:
    enum class Status {
        Disconnected,
        Connecting,
        Synchronizing,
        Synchronized
    };
    Q_ENUM(Status)

    struct Balance {
        quint64 confirmed = 0;
        quint64 unconfirmed = 0;   // incoming or change not yet confirmed
        quint64 total() const { return confirmed + unconfirmed; }
    };

    CoinWallet(HdAccount account, const QJsonObject &cache, QObject *parent = nullptr);

    const CoinParams &params() const { return m_account.params(); }
    Status status() const { return m_status; }
    QString serverName() const;
    int blockHeight() const { return m_height; }

    void setProxy(const QNetworkProxy &proxy);
    // Empty host = pick a random built-in server.
    void setCustomServer(const ElectrumServer &server);
    // Servers this wallet connects to, as "ssl://host:port" (or "tcp://" for
    // an onion server): the custom one, or all the built-in ones.
    QStringList electrumUrls() const;
    void start();
    void stop();
    bool isRunning() const { return m_running; }

    Balance balance() const;
    QList<electrum::HistoryEntry> history() const { return m_history; }
    // Block time of a confirmed transaction, or when it was first seen.
    QDateTime transactionTime(const QString &txid) const;

    // Next address never used to receive (a new one each time a payment arrives).
    QString receiveAddress() const;
    // First receive address: identifies the seed without exposing it.
    QString firstAddress() const { return m_account.address(HdAccount::Receive, 0); }

    double feeRate(int targetBlocks) const;   // sat/vB

    // Coin control: `onlyCoins` (coinKey values) restricts the spend to those
    // coins, frozen or not; without it, frozen coins are never spent.
    std::optional<TxPlan> planSend(const QString &address, quint64 amount, double feeRate, bool sendAll,
                                   QString *error, const QStringList &onlyCoins = {}) const;

    // The wallet's unspent coins, and freezing (kept in the encrypted file).
    QList<electrum::WalletUtxo> coins() const { return m_utxos; }
    static QString coinKey(const Utxo &utxo) { return QString("%1:%2").arg(utxo.txid).arg(utxo.vout); }
    bool isFrozen(const Utxo &utxo) const { return m_frozen.contains(coinKey(utxo)); }
    void setFrozen(const QStringList &coinKeys, bool frozen);
    // Replace-by-fee for one of our unconfirmed transactions: same payment,
    // higher fee (see planFeeBump). Refused when a later transaction spends
    // its outputs, which the replacement would cancel.
    std::optional<TxPlan> planBump(const QString &txid, double feeRate, QString *error) const;
    // Fee and fee rate (sat/vB) of one of our transactions, if known.
    std::optional<quint64> transactionFee(const QString &txid) const;
    double transactionFeeRate(const QString &txid) const;

    // Signs and broadcasts a plan the user has confirmed.
    void broadcast(const TxPlan &plan, std::function<void(const QString &txid, const QString &error)> callback);

    // Everything worth keeping for a fast restart (no secret inside).
    QJsonObject cache() const;

signals:
    void statusChanged(Status status);
    void updated();          // balance, history or height changed
    void cacheChanged();     // cache() should be saved

private:
    void connectToServer();
    void onReady();
    void onFailed(const QString &reason);
    void onRequestError(const QString &error);
    void onNotification(const QString &method, const QJsonArray &params);
    void scanNext();
    void fetchMissingTransactions(std::function<void()> then);
    void fetchBlockTimes(std::function<void()> then);
    void refreshFees();
    void recompute();
    void setStatus(Status status);
    QString scriptHash(const electrum::AddressRef &ref) const;

    HdAccount m_account;
    ElectrumClient *m_client;
    ElectrumClient::PinStore m_pins;
    ElectrumServer m_customServer;
    QNetworkProxy m_proxy{QNetworkProxy::NoProxy};
    Status m_status = Status::Disconnected;
    bool m_running = false;
    int m_failures = 0;

    electrum::GapScanner m_scanner;
    int m_pendingScans = 0;
    QMap<QByteArray, electrum::AddressRef> m_scripts;      // scriptPubKey -> address
    QMap<QString, electrum::AddressRef> m_scriptHashes;    // electrum scripthash -> address
    QMap<QString, QString> m_rawTxs;                        // txid -> hex (cache)
    QMap<QString, int> m_heights;                           // txid -> height
    QMap<QString, electrum::ParsedTx> m_parsed;
    QMap<int, double> m_feeRates;                           // target blocks -> sat/vB
    QMap<int, qint64> m_blockTimes;                         // height -> unix time (cache)
    QMap<QString, qint64> m_firstSeen;                      // txid -> unix time (cache)
    QList<electrum::HistoryEntry> m_history;
    QList<electrum::WalletUtxo> m_utxos;
    QSet<QString> m_frozen;   // coinKey values
    int m_height = 0;
};

}

#endif // BISCUIT_COINWALLET_H
