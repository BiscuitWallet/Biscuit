// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_DEMOSWAPPROVIDER_H
#define BISCUIT_DEMOSWAPPROVIDER_H

#include "SwapProvider.h"

namespace biscuit::swap {

// Offline provider used when Biscuit is built without a Trocador API key.
// See swap/core/DemoSwap.h. Answers after a short delay to mimic the network.
class DemoSwapProvider : public SwapProvider {
    Q_OBJECT

public:
    explicit DemoSwapProvider(QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QString kycPolicy() const override;
    QString supportUrl() const override;
    bool isDemo() const override { return true; }

    void supportedAssets(AssetsCallback callback) override;
    void getQuotes(const QuoteRequest &request, QuotesCallback callback) override;
    void createTrade(const TradeRequest &request, TradeCallback callback) override;
    void getTradeStatus(const Trade &trade, TradeCallback callback) override;
    void validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) override;

private:
    void later(std::function<void()> fn);
};

}

#endif // BISCUIT_DEMOSWAPPROVIDER_H
