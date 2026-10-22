// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Answers of the relay's /api/x/ routes (biscuit-relay, exchanges.go).
// URLs stay out of raw strings: moc misreads "//" there and skips the class.

#include <QtTest>

#include "ExchangesApi.h"

using namespace biscuit::swap;

namespace {
    const QString xmrAddress = "87bsQNEnRRe56A6ERMLmwDCanjkoN6Q2eFwCFqi5tLAQcwzvxDj6rxZUAa2rRPqvX8a6fdpoerpxnNSRTPyAFt4zJsDvZcB";

    QuoteRequest btcToXmr(RateType type = RateType::Floating) {
        QuoteRequest r;
        r.from = {"btc", "Mainnet"};
        r.to = {"xmr", "Mainnet"};
        r.amountFrom = "0.01";
        r.amountTo = "1.5";
        r.rateType = type;
        return r;
    }

    const QByteArray tradeResponse = R"({
        "id": "pegasus-F723NT", "exchange": "pegasus", "name": "PegasusSwap", "kyc": "A",
        "exchange_id": "F723NT",
        "from": {"ticker": "btc", "network": "Mainnet"}, "to": {"ticker": "xmr", "network": "Mainnet"},
        "amount_from": "0.01", "amount_to": "1.541617520325", "fixed": false,
        "deposit_address": "bc1qdeposit000000000000000000000000000000", "deposit_memo": "",
        "address": "87bsQNEnRRe56A6ERMLmwDCanjkoN6Q2eFwCFqi5tLAQcwzvxDj6rxZUAa2rRPqvX8a6fdpoerpxnNSRTPyAFt4zJsDvZcB",
        "memo": "", "refund": "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
        "status": "waiting", "created_at": "2026-10-12T18:00:00.000Z"
    })";

    QByteArray tradeBody() {
        QByteArray body = tradeResponse;
        body.replace("\"exchange_id\": \"F723NT\",", "\"exchange_id\": \"F723NT\", \"support\": \"https://pegasusswap.com/support\",");
        return body;
    }
}

class TestExchangesApi : public QObject {
    Q_OBJECT

private slots:
    void quotesQuery() {
        QUrlQuery q = exchanges::quotesQuery(btcToXmr());
        QCOMPARE(q.queryItemValue("from"), QString("btc"));
        QCOMPARE(q.queryItemValue("to_network"), QString("Mainnet"));
        QCOMPARE(q.queryItemValue("amount"), QString("0.01"));
        QCOMPARE(q.queryItemValue("fixed"), QString("0"));

        q = exchanges::quotesQuery(btcToXmr(RateType::Fixed));
        QCOMPARE(q.queryItemValue("amount"), QString("1.5"));   // the amount received
        QCOMPARE(q.queryItemValue("fixed"), QString("1"));
    }

    void parseAssets() {
        const auto assets = exchanges::parseAssets(R"json({"assets": [
            {"ticker": "xmr", "network": "Mainnet", "name": "Monero", "memo": false},
            {"ticker": "xrp", "network": "Mainnet", "name": "XRP", "memo": true},
            {"ticker": "usdt", "network": "TRC20", "name": "Tether (Tron)"}
        ]})json", nullptr);
        QVERIFY(assets.has_value());
        QCOMPARE(assets->size(), 3);
        QVERIFY(!assets->at(0).hasMemo);
        QVERIFY(assets->at(1).hasMemo);
        QCOMPARE(assets->at(2).asset, Asset({"usdt", "TRC20"}));
        QVERIFY(!assets->at(2).hasMemo);
    }

    void parseQuotes() {
        const QByteArray body = R"({"quotes": [
            {"exchange": "pegasus", "name": "PegasusSwap", "kyc": "A", "amount_from": "0.01", "amount_to": "1.541617520325", "fixed": false},
            {"exchange": "bad", "name": "Bad", "kyc": "A", "amount_from": "0.01", "amount_to": "0", "fixed": false},
            {"exchange": "fixed", "name": "Fixed", "kyc": "A", "amount_from": "0.01", "amount_to": "1.5", "fixed": true}
        ], "errors": ["Other: the minimum is 0.02 BTC."]})";
        QString error;
        const auto result = exchanges::parseQuotes(body, btcToXmr(), &error);
        QVERIFY2(result, qPrintable(error));
        QCOMPARE(result->quotes.size(), 1);   // zero amount and wrong rate type dropped
        const Quote &q = result->quotes.first();
        QCOMPARE(q.providerId, QString("exchanges"));
        QCOMPARE(q.exchange, QString("PegasusSwap"));
        QCOMPARE(q.rateId, QString("pegasus"));
        QCOMPARE(q.amountTo, QString("1.541617520325"));
        QCOMPARE(q.kycRating, KycRating::A);
        QCOMPARE(result->errors, QStringList{"Other: the minimum is 0.02 BTC."});
    }

    void parseRelayError() {
        QString error;
        QVERIFY(!exchanges::parseQuotes(R"({"error":"rate_limited","message":"Too many requests."})", btcToXmr(), &error));
        QCOMPARE(error, QString("Too many requests."));
        QVERIFY(!exchanges::parseTrade("<html>", &error));
    }

    void newTradeQuery() {
        TradeRequest r;
        r.quote.rateId = "pegasus";
        r.quote.from = {"btc", "Mainnet"};
        r.quote.to = {"xmr", "Mainnet"};
        r.quote.amountFrom = "0.01";
        r.quote.amountTo = "1.54";
        r.payoutAddress = xmrAddress;
        r.refundAddress = "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq";
        QUrlQuery q = exchanges::newTradeQuery(r);
        QCOMPARE(q.queryItemValue("exchange"), QString("pegasus"));
        QCOMPARE(q.queryItemValue("amount"), QString("0.01"));
        QCOMPARE(q.queryItemValue("address"), xmrAddress);
        QCOMPARE(q.queryItemValue("refund"), r.refundAddress);
        QVERIFY(!q.hasQueryItem("memo"));

        r.quote.rateType = RateType::Fixed;
        QCOMPARE(exchanges::newTradeQuery(r).queryItemValue("amount"), QString("1.54"));
    }

    void parseTrade() {
        QString error;
        const auto t = exchanges::parseTrade(tradeBody(), &error);
        QVERIFY2(t, qPrintable(error));
        QCOMPARE(t->providerId, QString("exchanges"));
        QCOMPARE(t->tradeId, QString("pegasus-F723NT"));
        QCOMPARE(t->exchange, QString("PegasusSwap"));
        QCOMPARE(t->exchangeTradeId, QString("F723NT"));
        QCOMPARE(t->supportUrl, QString("https://pegasusswap.com/support"));
        QCOMPARE(t->from, (Asset{"btc", "Mainnet"}));
        QCOMPARE(t->amountTo, QString("1.541617520325"));
        QCOMPARE(t->depositAddress, QString("bc1qdeposit000000000000000000000000000000"));
        QCOMPARE(t->payoutAddress, xmrAddress);
        QCOMPARE(t->status, TradeStatus::Waiting);
        QCOMPARE(t->kycRating, KycRating::A);
        QVERIFY(t->createdAt.isValid());

        // The status query proves it is ours with the payout address.
        const QUrlQuery q = exchanges::tradeQuery(*t);
        QCOMPARE(q.queryItemValue("id"), QString("pegasus-F723NT"));
        QCOMPARE(q.queryItemValue("address"), xmrAddress);
    }

    void parseTradeRejectsIncomplete() {
        QByteArray body = tradeBody();
        body.replace("\"deposit_address\": \"bc1qdeposit000000000000000000000000000000\"", "\"deposit_address\": \"\"");
        QString error;
        QVERIFY(!exchanges::parseTrade(body, &error));
    }
};

QTEST_GUILESS_MAIN(TestExchangesApi)
#include "test_exchanges_api.moc"
