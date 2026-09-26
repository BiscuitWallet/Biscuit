// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include "Amount.h"
#include "DemoSwap.h"
#include "QuoteRanking.h"

using namespace biscuit::swap;

namespace {
    QuoteRequest request(const QString &amountFrom = "2") {
        QuoteRequest r;
        r.from = {"xmr", "Mainnet"};
        r.to = {"btc", "Mainnet"};
        r.amountFrom = amountFrom;
        r.minKycRating = KycRating::D;
        return r;
    }

    // Characters allowed in Monero/Bitcoin/Litecoin base58 addresses.
    const QString base58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
}

class TestDemoSwap : public QObject
{
Q_OBJECT

private slots:
    void fixedRateGivesTheExactAmountReceived() {
        QuoteRequest r = request();
        r.amountFrom.clear();
        r.amountTo = "0.5";
        r.rateType = RateType::Fixed;
        const auto quotes = demo::quotes(r);
        QVERIFY(!quotes.isEmpty());
        for (const Quote &q : quotes) {
            QCOMPARE(q.amountTo, QString("0.5"));
            QVERIFY(amount::isValid(q.amountFrom) && !amount::isZero(q.amountFrom));
        }
    }

    void quotesAreDeterministic() {
        const auto a = demo::quotes(request());
        const auto b = demo::quotes(request());
        QCOMPARE(a.size(), 4);
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(a.at(i).amountTo, b.at(i).amountTo);
            QCOMPARE(a.at(i).rateId, b.at(i).rateId);
            QVERIFY(amount::isValid(a.at(i).amountTo));
        }
    }

    void unsupportedRequestsGiveNoQuote() {
        QuoteRequest same = request();
        same.to = same.from;
        QVERIFY(demo::quotes(same).isEmpty());

        QuoteRequest unknown = request();
        unknown.to = {"doge", "Mainnet"};
        QVERIFY(demo::quotes(unknown).isEmpty());

        QVERIFY(demo::quotes(request("0")).isEmpty());
        QVERIFY(demo::quotes(request("abc")).isEmpty());
    }

    void bestDemoOfferIsNotTheHaltingOne() {
        const auto ranked = rankQuotes(demo::quotes(request()), KycRating::D);
        QVERIFY(!ranked.first().exchange.contains("Halt"));
    }

    void depositAddressCanNeverReceiveFunds() {
        const auto quote = demo::quotes(request()).first();
        const Trade t = demo::createTrade({quote, "bc1qpayout", {}, "4refund", {}}, QDateTime::currentDateTimeUtc());

        QVERIFY(demo::isDemoAddress(t.depositAddress));
        bool hasNonBase58 = false;
        for (const QChar c : t.depositAddress) {
            hasNonBase58 |= !base58.contains(c);
        }
        QVERIFY2(hasNonBase58, "demo deposit address must be invalid on every chain");
        QCOMPARE(t.providerId, QString("demo"));
        QCOMPARE(t.status, TradeStatus::Waiting);
    }

    void simulatedProgress() {
        const QDateTime start = QDateTime::fromString("2026-09-24T12:00:00Z", Qt::ISODate);
        auto quotes = demo::quotes(request());
        const Trade normal = demo::createTrade({quotes.first(), "bc1q", {}, "4r", {}}, start);
        QCOMPARE(demo::statusAt(normal, start.addSecs(10)), TradeStatus::Waiting);
        QCOMPARE(demo::statusAt(normal, start.addSecs(70)), TradeStatus::Confirming);
        QCOMPARE(demo::statusAt(normal, start.addSecs(130)), TradeStatus::Sending);
        QCOMPARE(demo::statusAt(normal, start.addSecs(200)), TradeStatus::Finished);

        Quote haltQuote;
        for (const auto &q : quotes) {
            if (q.exchange.contains("Halt")) haltQuote = q;
        }
        const Trade halting = demo::createTrade({haltQuote, "bc1q", {}, "4r", {}}, start);
        QCOMPARE(demo::statusAt(halting, start.addSecs(100)), TradeStatus::Halted);
    }
};

QTEST_APPLESS_MAIN(TestDemoSwap)
#include "test_demo_swap.moc"
