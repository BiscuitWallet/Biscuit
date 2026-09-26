// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Response samples follow the fields used by Cake Wallet's Trocador
// integration. Replace them with real captured responses once an API key is
// available.

#include <QtTest>

#include "QuoteRanking.h"
#include "TrocadorApi.h"

using namespace biscuit::swap;

namespace {
    QuoteRequest xmrToBtc() {
        QuoteRequest r;
        r.from = {"xmr", "Mainnet"};
        r.to = {"btc", "Mainnet"};
        r.amountFrom = "1.5";
        r.minKycRating = KycRating::B;
        return r;
    }

    const QByteArray rateResponse = R"({
        "trade_id": "RATE42",
        "date": "2026-09-23 10:00:00",
        "ticker_from": "xmr", "ticker_to": "btc",
        "network_from": "Mainnet", "network_to": "Mainnet",
        "amount_from": 1.5, "amount_to": 0.0063,
        "provider": "Exch",
        "quotes": {"quotes": [
            {"provider": "Exch",        "kycrating": "A", "amount_to": "0.0063",  "eta": 20},
            {"provider": "ChangeNOW",   "kycrating": "C", "amount_to": 0.00641,   "eta": 15},
            {"provider": "WizardSwap",  "kycrating": "B", "amount_to": "0.00635", "eta": 30}
        ]}
    })";

    const QByteArray tradeResponse = R"({
        "trade_id": "TRADE77",
        "date": "2026-09-23T10:01:00",
        "ticker_from": "xmr", "ticker_to": "btc",
        "network_from": "Mainnet", "network_to": "Mainnet",
        "amount_from": 1.5, "amount_to": 0.00635,
        "provider": "WizardSwap",
        "id_provider": "wz-9",
        "password": "pw",
        "address_provider": "4DepositAddress",
        "address_provider_memo": "0",
        "address_user": "bc1qpayout",
        "address_user_memo": "0",
        "refund_address": "4RefundAddress",
        "refund_address_memo": "0",
        "status": "waiting"
    })";
}

class TestTrocadorApi : public QObject
{
Q_OBJECT

private slots:
    void rateQueryNeverAddsMarkup() {
        auto req = xmrToBtc();
        QCOMPARE(trocador::rateQuery(req).queryItemValue("markup"), QString("0"));
        req.rateType = RateType::Fixed;
        QCOMPARE(trocador::rateQuery(req).queryItemValue("markup"), QString("0"));
    }

    void rateQueryUsesTickerAndNetwork() {
        const QUrlQuery q = trocador::rateQuery(xmrToBtc());
        QCOMPARE(q.queryItemValue("ticker_from"), QString("xmr"));
        QCOMPARE(q.queryItemValue("network_from"), QString("Mainnet"));
        QCOMPARE(q.queryItemValue("ticker_to"), QString("btc"));
        QCOMPARE(q.queryItemValue("network_to"), QString("Mainnet"));
        QCOMPARE(q.queryItemValue("amount_from"), QString("1.5"));
        QCOMPARE(q.queryItemValue("min_kycrating"), QString("B"));
        QCOMPARE(q.queryItemValue("payment"), QString("False"));
    }

    void newTradeQuery() {
        TradeRequest req;
        req.quote.exchange = "WizardSwap";
        req.quote.from = {"xmr", "Mainnet"};
        req.quote.to = {"btc", "Mainnet"};
        req.quote.amountFrom = "1.50";
        req.payoutAddress = "bc1qpayout";
        req.refundAddress = "4RefundAddress";

        const QUrlQuery q = trocador::newTradeQuery(req, "RATE42");
        QCOMPARE(q.queryItemValue("id"), QString("RATE42"));
        QCOMPARE(q.queryItemValue("provider"), QString("WizardSwap"));
        QCOMPARE(q.queryItemValue("amount_from"), QString("1.5"));
        QCOMPARE(q.queryItemValue("address"), QString("bc1qpayout"));
        QCOMPARE(q.queryItemValue("refund"), QString("4RefundAddress"));
        QCOMPARE(q.queryItemValue("address_memo"), QString("0"));
        QCOMPARE(q.queryItemValue("refund_memo"), QString("0"));
        QCOMPARE(q.queryItemValue("markup"), QString("0"));
    }

    void parsesRates() {
        QString error;
        const auto result = trocador::parseRate(rateResponse, xmrToBtc(), &error);
        QVERIFY2(result.has_value(), qPrintable(error));
        QCOMPARE(result->rateId, QString("RATE42"));
        QCOMPARE(result->quotes.size(), 3);

        const Quote &cn = result->quotes.at(1);
        QCOMPARE(cn.exchange, QString("ChangeNOW"));
        QCOMPARE(cn.amountTo, QString("0.00641"));
        QCOMPARE(cn.kycRating, KycRating::C);
        QCOMPARE(cn.etaMinutes, std::optional<int>(15));
        QCOMPARE(cn.amountFrom, QString("1.5"));
        QCOMPARE(cn.providerId, QString("trocador"));
    }

    void parsedRatesRankWithKycFilter() {
        const auto result = trocador::parseRate(rateResponse, xmrToBtc(), nullptr);
        QVERIFY(result.has_value());
        // ChangeNOW pays most but is rated C: filtered out with min rating B.
        const auto ranked = rankQuotes(result->quotes, KycRating::B);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked.first().exchange, QString("WizardSwap"));
        QCOMPARE(rankQuotes(result->quotes, KycRating::C).first().exchange, QString("ChangeNOW"));
    }

    void parsesTrade() {
        QString error;
        const auto t = trocador::parseTrade(tradeResponse, &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tradeId, QString("TRADE77"));
        QCOMPARE(t->exchange, QString("WizardSwap"));
        QCOMPARE(t->exchangeTradeId, QString("wz-9"));
        QCOMPARE(t->exchangePassword, QString("pw"));
        QCOMPARE(t->depositAddress, QString("4DepositAddress"));
        QCOMPARE(t->depositMemo, QString());       // "0" means no memo
        QCOMPARE(t->payoutAddress, QString("bc1qpayout"));
        QCOMPARE(t->refundAddress, QString("4RefundAddress"));
        QCOMPARE(t->amountFrom, QString("1.5"));
        QCOMPARE(t->amountTo, QString("0.00635"));
        QCOMPARE(t->status, TradeStatus::Waiting);
        QCOMPARE(t->from, Asset({"xmr", "Mainnet"}));
    }

    void parsesTradeStatusList() {
        // The "trade" method returns a list with one trade.
        const QByteArray list = "[" + tradeResponse + "]";
        const auto t = trocador::parseTrade(list, nullptr);
        QVERIFY(t.has_value());
        QCOMPARE(t->tradeId, QString("TRADE77"));
    }

    void reportsApiErrors() {
        const QByteArray body = R"({"error": "Missing API key"})";
        QString error;
        QVERIFY(!trocador::parseRate(body, xmrToBtc(), &error).has_value());
        QCOMPARE(error, QString("Missing API key"));

        QVERIFY(!trocador::parseTrade(R"({"error":"Invalid","message":"bad address"})", &error).has_value());
        QCOMPARE(error, QString("Invalid: bad address"));

        QVERIFY(!trocador::parseTrade("not json", &error).has_value());
        QVERIFY(error.startsWith("Invalid JSON"));
    }

    void validateAddress() {
        const QUrlQuery q = trocador::validateAddressQuery({"btc", "Mainnet"}, "bc1qxyz");
        QCOMPARE(q.queryItemValue("ticker"), QString("btc"));
        QCOMPARE(q.queryItemValue("network"), QString("Mainnet"));
        QCOMPARE(q.queryItemValue("address"), QString("bc1qxyz"));
        QCOMPARE(trocador::parseValidateAddress(R"({"result": true})", nullptr), std::optional<bool>(true));
        QCOMPARE(trocador::parseValidateAddress(R"({"result": false})", nullptr), std::optional<bool>(false));
        QVERIFY(!trocador::parseValidateAddress(R"({"other": 1})", nullptr).has_value());
    }

    void parsesCoins() {
        const QByteArray body = R"([
            {"name": "Monero", "ticker": "xmr", "network": "Mainnet", "memo": false, "minimum": 0.01, "maximum": 100},
            {"name": "Tether", "ticker": "USDT", "network": "TRC20", "memo": false, "minimum": "10", "maximum": "50000"},
            {"name": "Broken"}
        ])";
        QString error;
        const auto coins = trocador::parseCoins(body, &error);
        QVERIFY2(coins.has_value(), qPrintable(error));
        QCOMPARE(coins->size(), 2);
        QCOMPARE(coins->at(1).asset, Asset({"usdt", "TRC20"}));
        QCOMPARE(coins->at(0).minimum, QString("0.01"));
        QCOMPARE(coins->at(1).maximum, QString("50000"));
    }

    void relayErrorsAreNotTrocadorErrors() {
        QCOMPARE(trocador::parseRelayError(R"({"error":"tor_exit","message":"Not through Tor."})"),
                 QString("Not through Tor."));
        QCOMPARE(trocador::parseRelayError(R"({"error":"rate_limited","message":"Slow down."})"),
                 QString("Slow down."));
        // Trocador's own errors, including a rejected key, are left to the caller.
        QVERIFY(trocador::parseRelayError(R"({"error":"Invalid API key"})").isEmpty());
        QVERIFY(trocador::parseRelayError(R"({"trade_id":"ABC"})").isEmpty());
        QVERIFY(trocador::parseRelayError("not json").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestTrocadorApi)
#include "test_trocador_api.moc"
