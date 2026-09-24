// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPPROVIDER_H
#define BISCUIT_SWAPPROVIDER_H

#include <functional>
#include <optional>

#include <QObject>

#include "SwapTypes.h"

namespace biscuit::swap {

// A swap service (aggregator or exchange). All calls are asynchronous and
// answer exactly once through their callback, on the Qt main thread.
// Network requests must go through utils/Networking so the Tor setting applies.
class SwapProvider : public QObject {
    Q_OBJECT

public:
    using AssetsCallback = std::function<void(const QList<AssetInfo> &assets, const QString &error)>;
    using QuotesCallback = std::function<void(const QList<Quote> &quotes, const QString &error)>;
    using TradeCallback = std::function<void(const std::optional<Trade> &trade, const QString &error)>;
    using ValidationCallback = std::function<void(std::optional<bool> valid, const QString &error)>;

    explicit SwapProvider(QObject *parent = nullptr) : QObject(parent) {}

    // Metadata
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString kycPolicy() const = 0;
    virtual QString supportUrl() const = 0;
    virtual bool isDemo() const { return false; }

    // Assets the provider can swap; any two different assets form a pair.
    virtual void supportedAssets(AssetsCallback callback) = 0;
    virtual void getQuotes(const QuoteRequest &request, QuotesCallback callback) = 0;
    virtual void createTrade(const TradeRequest &request, TradeCallback callback) = 0;
    virtual void getTradeStatus(const Trade &trade, TradeCallback callback) = 0;
    virtual void validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) = 0;
};

}

#endif // BISCUIT_SWAPPROVIDER_H
