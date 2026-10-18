// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPMANAGER_H
#define BISCUIT_SWAPMANAGER_H

#include <QObject>
#include <QPointer>
#include <QDateTime>
#include <QHash>
#include <QTimer>

#include "SwapHistory.h"
#include "SwapProvider.h"

class Wallet;

namespace biscuit::swap {

// Swaps of one open wallet: asks all enabled providers for quotes, ranks them
// neutrally, creates trades, follows their status and keeps the local history
// in the wallet cache.
class SwapManager : public QObject {
    Q_OBJECT

public:
    explicit SwapManager(Wallet *wallet, QObject *parent = nullptr);

    // Built-in providers minus the ones disabled in the settings.
    QList<SwapProvider *> providers() const;
    SwapProvider *provider(const QString &id) const;
    bool demoMode() const;


    using QuotesCallback = std::function<void(const QList<Quote> &ranked, const QStringList &errors)>;
    // Queries every provider in parallel, then ranks all offers together.
    void requestQuotes(const QuoteRequest &request, QuotesCallback callback);

    void validateAddress(const Asset &asset, const QString &address, SwapProvider::ValidationCallback callback);
    void createTrade(const TradeRequest &request, SwapProvider::TradeCallback callback);

    QList<Trade> trades() const;
    const Trade *trade(const QString &providerId, const QString &tradeId) const;

    // Records the Monero transaction that paid the deposit of a trade.
    void setDepositTx(const QString &providerId, const QString &tradeId, const QString &txid);

    void refreshNow();

signals:
    void tradesChanged();

private:
    void poll(bool force = false);
    void mergeStatus(const Trade &update);
    void save();
    void flushIfPossible();

    QPointer<Wallet> m_wallet;
    QList<SwapProvider *> m_allProviders;
    SwapHistory m_history;
    QTimer m_pollTimer;
    QHash<QString, QDateTime> m_lastCheck;   // provider:trade -> last status request
    bool m_dirty = false;
    int m_quoteGeneration = 0;
};

}

#endif // BISCUIT_SWAPMANAGER_H
