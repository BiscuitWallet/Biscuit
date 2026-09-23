// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Neutral ranking is a non-negotiable principle of Biscuit: the best offer is
// the one where the user receives the most. These tests guard it.

#include <algorithm>
#include <random>

#include <QtTest>

#include "QuoteRanking.h"

using namespace biscuit::swap;

namespace {
    Quote quote(const QString &exchange, const QString &amountTo, KycRating kyc = KycRating::A,
                std::optional<int> eta = std::nullopt, const QString &provider = "trocador") {
        Quote q;
        q.providerId = provider;
        q.exchange = exchange;
        q.rateId = "rate";
        q.from = {"xmr", "Mainnet"};
        q.to = {"btc", "Mainnet"};
        q.amountFrom = "1";
        q.amountTo = amountTo;
        q.kycRating = kyc;
        q.etaMinutes = eta;
        return q;
    }

    QStringList exchanges(const QList<Quote> &quotes) {
        QStringList names;
        for (const auto &q : quotes) {
            names << q.exchange;
        }
        return names;
    }
}

class TestQuoteRanking : public QObject
{
Q_OBJECT

private slots:
    void bestAmountReceivedFirst() {
        const QList<Quote> quotes = {
            quote("Low", "0.0040"),
            quote("High", "0.0042"),
            quote("Mid", "0.0041"),
        };
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::D)), QStringList({"High", "Mid", "Low"}));
    }

    void apiOrderIsIgnored() {
        // Trocador already sorts its answer, but we never rely on it.
        QList<Quote> quotes = {
            quote("A", "0.001"), quote("B", "0.005"), quote("C", "0.003"),
            quote("D", "0.004"), quote("E", "0.002"),
        };
        const QStringList expected = {"B", "D", "C", "E", "A"};

        std::mt19937 rng(42);
        for (int i = 0; i < 20; ++i) {
            std::shuffle(quotes.begin(), quotes.end(), rng);
            QCOMPARE(exchanges(rankQuotes(quotes, KycRating::D)), expected);
        }
    }

    void exchangeNameDoesNotBeatAmount() {
        // An exchange's name, provider module or position must never beat a
        // better amount. "Aaa" sorts first alphabetically but receives less.
        const QList<Quote> quotes = {
            quote("Aaa", "0.00419999", KycRating::A, 1, "aaa-partner"),
            quote("Zzz", "0.0042", KycRating::A, 500, "zzz-partner"),
        };
        QCOMPARE(rankQuotes(quotes, KycRating::D).first().exchange, QString("Zzz"));
    }

    void exactDecimalComparison() {
        // Differences beyond double precision must still be ranked correctly.
        const QList<Quote> quotes = {
            quote("Rounded", "1.00000000000000001"),
            quote("Exact", "1.00000000000000002"),
        };
        QCOMPARE(rankQuotes(quotes, KycRating::D).first().exchange, QString("Exact"));
    }

    void etaBreaksTies() {
        const QList<Quote> quotes = {
            quote("Slow", "0.0042", KycRating::A, 60),
            quote("NoEta", "0.0042", KycRating::A),
            quote("Fast", "0.0042", KycRating::A, 10),
        };
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::D)), QStringList({"Fast", "Slow", "NoEta"}));
    }

    void kycFilter() {
        const QList<Quote> quotes = {
            quote("RatingD", "0.0050", KycRating::D),
            quote("RatingC", "0.0045", KycRating::C),
            quote("RatingA", "0.0040", KycRating::A),
            quote("Unknown", "0.0060", KycRating::Unknown),
        };
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::A)), QStringList({"RatingA"}));
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::C)), QStringList({"RatingC", "RatingA"}));
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::D)), QStringList({"RatingD", "RatingC", "RatingA"}));
    }

    void invalidAmountsDropped() {
        const QList<Quote> quotes = {
            quote("Zero", "0"),
            quote("Empty", ""),
            quote("Garbage", "n/a"),
            quote("Ok", "0.001"),
        };
        QCOMPARE(exchanges(rankQuotes(quotes, KycRating::D)), QStringList({"Ok"}));
    }

    void deterministicForEqualOffers() {
        const QList<Quote> a = {quote("Beta", "1"), quote("alpha", "1")};
        const QList<Quote> b = {quote("alpha", "1"), quote("Beta", "1")};
        QCOMPARE(exchanges(rankQuotes(a, KycRating::D)), exchanges(rankQuotes(b, KycRating::D)));
    }
};

QTEST_APPLESS_MAIN(TestQuoteRanking)
#include "test_quote_ranking.moc"
