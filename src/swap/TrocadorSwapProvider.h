// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_TROCADORSWAPPROVIDER_H
#define BISCUIT_TROCADORSWAPPROVIDER_H

#include <QUrlQuery>

#include "SwapProvider.h"

class QNetworkReply;

namespace biscuit::swap {

// Trocador aggregator (https://trocador.app). Official builds go through the
// Biscuit relay, which adds the partner key server side (relay/README.md).
// Development builds may call Trocador directly with a key from secrets.cmake.
class TrocadorSwapProvider : public SwapProvider {
    Q_OBJECT

public:
    // baseUrl ends with '/'. apiKey is empty when baseUrl is the relay.
    TrocadorSwapProvider(const QString &baseUrl, const QString &apiKey, QObject *parent = nullptr);

    // API base configured for this build: the relay, or Trocador itself in a
    // development build with a key. Empty: swaps run in demo mode.
    static QString builtInBaseUrl();

    // Key compiled into a development build, empty otherwise.
    static QString builtInApiKey();

    // Trocador requires the IP address each swap is created from to be recorded, so the
    // relay refuses to create swaps from Tor exits. In Tor mode
    // (any proxy enabled) exchange swaps are off, status checks included.
    static bool proxyActive();

    // Set when Trocador rejects the built-in key: swaps stay off (no retries)
    // until an update ships a new key.
    static bool keyRejected();

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
    void get(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt = 0);
    void send(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt = 0);

    QString m_baseUrl;
    QString m_apiKey;
};

}

#endif // BISCUIT_TROCADORSWAPPROVIDER_H
