// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_TROCADORSWAPPROVIDER_H
#define BISCUIT_TROCADORSWAPPROVIDER_H

#include <QUrlQuery>

#include "SwapProvider.h"

class QNetworkReply;

namespace biscuit::swap {

// Trocador aggregator (https://trocador.app). Requires an API key injected at
// build time (see BUILD.md), never stored in the repository.
class TrocadorSwapProvider : public SwapProvider {
    Q_OBJECT

public:
    explicit TrocadorSwapProvider(const QString &apiKey, QObject *parent = nullptr);

    // Key compiled into this build, empty if none.
    static QString builtInApiKey();

    QString id() const override;
    QString displayName() const override;
    QString kycPolicy() const override;
    QString supportUrl() const override;

    void supportedAssets(AssetsCallback callback) override;
    void getQuotes(const QuoteRequest &request, QuotesCallback callback) override;
    void createTrade(const TradeRequest &request, TradeCallback callback) override;
    void getTradeStatus(const Trade &trade, TradeCallback callback) override;
    void validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) override;

private:
    using ReplyHandler = std::function<void(const QByteArray &body, const QString &networkError)>;
    void get(const QString &method, const QUrlQuery &query, ReplyHandler handler);

    QString m_apiKey;
    QList<AssetInfo> m_assetsCache;
};

}

#endif // BISCUIT_TROCADORSWAPPROVIDER_H
