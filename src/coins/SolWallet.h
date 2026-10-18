// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SOLWALLET_H
#define BISCUIT_SOLWALLET_H

#include <functional>
#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "core/Sol.h"
#include "core/SolApi.h"

namespace biscuit::coins {

// A Solana wallet: SOL on one address, from the BIP39 seed
// (m/44'/501'/0'/0', as Phantom). Balance, fees and history come from public
// nodes (one at a time, the next one if it fails), never from another
// network. Keeps a cache (balance, history) that the owner stores in the
// encrypted wallet file.
class SolWallet : public QObject {
    Q_OBJECT

public:
    enum class Status { Disconnected, Connecting, Synchronized };
    Q_ENUM(Status)

    SolWallet(sol::Account account, const QJsonObject &cache, QObject *parent = nullptr);

    QByteArray address() const { return m_account.publicKey(); }
    QString addressText() const { return sol::addressText(address()); }
    Status status() const { return m_status; }
    QString nodeName() const;

    quint64 balance() const { return m_balance; }   // lamports
    // Newest first, pending sends on top.
    QList<sol::HistoryEntry> history() const;
    // Network fee of a send now (signature + priority fee), in lamports.
    std::optional<quint64> transferFee() const;
    // What "all" sends: the balance less the fee.
    quint64 sendable() const;

    void setCustomNode(const QString &url);   // empty = the public nodes
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    void refresh();

    // A send, checked and priced, waiting for the user's confirmation.
    struct Plan {
        QByteArray recipient;
        quint64 amount = 0;
        quint64 fee = 0;
        // The recipient holds no SOL (a new address, or an emptied one).
        // Solana addresses have no checksum: said, so a typo can be caught.
        bool emptyRecipient = false;
        sol::Transfer transfer;
        quint64 lastValidBlockHeight = 0;
    };
    // `amountText` is a decimal amount or "all". Asks the node for a recent
    // blockhash and the recipient's balance, so the answer comes later.
    void planSend(const QString &address, const QString &amountText,
                  std::function<void(std::optional<Plan> plan, const QString &error)> done);
    // Signs, reads the signed bytes back to make sure they say exactly what
    // the plan says, and sends them to every node.
    void broadcast(const Plan &plan, std::function<void(const QString &signature, const QString &error)> done);

    QJsonObject cache() const;

signals:
    void statusChanged(Status status);
    void updated();          // balance, fees or history changed
    void cacheChanged();     // cache() should be saved

private:
    using RpcDone = std::function<void(std::optional<QJsonValue> result, const QString &error)>;
    void call(const QString &method, const QJsonArray &params, RpcDone done, int attempt = 0);
    QStringList nodes() const;
    bool networkAllowed() const;
    void setStatus(Status status);
    void refreshNode();
    void refreshHistory();
    void fetchTransactions(QStringList signatures);
    void checkSent();

    sol::Account m_account;
    QString m_customNode;
    int m_node = 0;
    bool m_running = false;
    Status m_status = Status::Disconnected;
    QTimer m_timer;
    quint64 m_generation = 0;

    quint64 m_balance = 0;
    std::optional<quint64> m_priorityFee;   // micro-lamports per compute unit
    QHash<QString, sol::HistoryEntry> m_history;   // key: signature
    struct Sent {
        sol::HistoryEntry entry;
        quint64 lastValidBlockHeight = 0;
    };
    QHash<QString, Sent> m_sent;          // broadcast here, not seen in a block yet
    QSet<QString> m_ignored;              // signatures with nothing for us
    QSet<QString> m_unreadable;           // of a transaction version newer than Biscuit
    bool m_historyComplete = false;       // the older ones were listed once
    bool m_fetching = false;
};

}

#endif // BISCUIT_SOLWALLET_H
