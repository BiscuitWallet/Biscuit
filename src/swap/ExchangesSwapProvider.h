// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_EXCHANGESSWAPPROVIDER_H
#define BISCUIT_EXCHANGESSWAPPROVIDER_H

#include <QUrlQuery>

#include "SwapProvider.h"

namespace biscuit::swap {

// Biscuit's own exchanges, through the relay's /api/x/ routes: exchanges that
// never ask for KYC and accept swaps created through Tor (PegasusSwap first).
// Their keys stay on the relay. Outside Tor mode the app reaches the relay
// directly; in Tor mode it uses the relay's onion address, so the traffic
// never leaves Tor. Nothing about the user is sent besides the swap itself.
class ExchangesSwapProvider : public SwapProvider {
    Q_OBJECT

public:
    // baseUrl: the relay's /api/x/, ending with '/'.
    explicit ExchangesSwapProvider(const QString &baseUrl, QObject *parent = nullptr);

    // Relay of this build (from BISCUIT_TROCADOR_RELAY_URL), empty without one.
    static QString builtInBaseUrl();

    QString id() const override;
    QString displayName() const override;
    QString kycPolicy() const override;
    QString supportUrl() const override;
    bool available() const override;

    void supportedAssets(AssetsCallback callback) override;
    void getQuotes(const QuoteRequest &request, QuotesCallback callback) override;
    void createTrade(const TradeRequest &request, TradeCallback callback) override;
    void getTradeStatus(const Trade &trade, TradeCallback callback) override;
    // Checked locally, nothing is sent.
    void validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) override;

private:
    using ReplyHandler = std::function<void(const QByteArray &body, const QString &networkError)>;
    void get(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt = 0);
    void send(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt);
    QString baseUrl() const;

    QString m_baseUrl;
};

}

#endif // BISCUIT_EXCHANGESSWAPPROVIDER_H
