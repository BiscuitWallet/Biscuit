// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "DemoSwapProvider.h"

#include <QTimer>

#include "DemoSwap.h"

namespace biscuit::swap {

DemoSwapProvider::DemoSwapProvider(QObject *parent)
    : SwapProvider(parent)
{
}

QString DemoSwapProvider::id() const {
    return demo::providerId;
}

QString DemoSwapProvider::displayName() const {
    return "Demo (no API key)";
}

QString DemoSwapProvider::kycPolicy() const {
    return "Simulated offers, no real exchange is contacted.";
}

QString DemoSwapProvider::supportUrl() const {
    return {};
}

void DemoSwapProvider::later(std::function<void()> fn) {
    QTimer::singleShot(400, this, std::move(fn));
}

void DemoSwapProvider::supportedAssets(AssetsCallback callback) {
    later([callback] { callback(demo::assets(), {}); });
}

void DemoSwapProvider::getQuotes(const QuoteRequest &request, QuotesCallback callback) {
    later([request, callback] {
        const auto quotes = demo::quotes(request);
        callback(quotes, quotes.isEmpty() ? "No demo offer for this pair or amount" : QString());
    });
}

void DemoSwapProvider::createTrade(const TradeRequest &request, TradeCallback callback) {
    later([request, callback] {
        callback(demo::createTrade(request, QDateTime::currentDateTimeUtc()), {});
    });
}

void DemoSwapProvider::getTradeStatus(const Trade &trade, TradeCallback callback) {
    later([trade, callback] {
        Trade updated = trade;
        updated.status = demo::statusAt(trade, QDateTime::currentDateTimeUtc());
        callback(updated, {});
    });
}

void DemoSwapProvider::validateAddress(const Asset &, const QString &address, ValidationCallback callback) {
    // No real check possible offline: accept anything plausible.
    later([address, callback] {
        callback(address.trimmed().size() >= 20, {});
    });
}

}
