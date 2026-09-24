// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "SwapHistory.h"

using namespace biscuit::swap;

namespace {
    Trade trade(const QString &id, TradeStatus status, const QDateTime &createdAt) {
        Trade t;
        t.providerId = "trocador";
        t.tradeId = id;
        t.from = {"xmr", "Mainnet"};
        t.to = {"btc", "Mainnet"};
        t.amountFrom = "1";
        t.amountTo = "0.004";
        t.depositAddress = "4deposit";
        t.status = status;
        t.createdAt = createdAt;
        t.updatedAt = createdAt;
        return t;
    }

    const QDateTime now = QDateTime::fromString("2026-09-24T12:00:00Z", Qt::ISODate);
}

class TestSwapHistory : public QObject
{
Q_OBJECT

private slots:
    void emptyOrGarbageInput() {
        QCOMPARE(SwapHistory::fromJson("").size(), 0);
        QCOMPARE(SwapHistory::fromJson("not json").size(), 0);
    }

    void roundTrip() {
        SwapHistory h;
        h.upsert(trade("A", TradeStatus::Waiting, now.addSecs(-60)));
        h.upsert(trade("B", TradeStatus::Finished, now.addDays(-1)));

        const SwapHistory back = SwapHistory::fromJson(h.toJson());
        QCOMPARE(back.size(), 2);
        QVERIFY(back.find("trocador", "A"));
        QCOMPARE(back.find("trocador", "B")->status, TradeStatus::Finished);
    }

    void upsertReplacesSameTrade() {
        SwapHistory h;
        h.upsert(trade("A", TradeStatus::Waiting, now));
        Trade updated = trade("A", TradeStatus::Confirming, now);
        h.upsert(updated);
        QCOMPARE(h.size(), 1);
        QCOMPARE(h.find("trocador", "A")->status, TradeStatus::Confirming);

        // Same id at another provider is another trade.
        Trade other = trade("A", TradeStatus::Waiting, now);
        other.providerId = "demo";
        h.upsert(other);
        QCOMPARE(h.size(), 2);
    }

    void newestFirst() {
        SwapHistory h;
        h.upsert(trade("old", TradeStatus::Finished, now.addDays(-3)));
        h.upsert(trade("new", TradeStatus::Waiting, now));
        h.upsert(trade("mid", TradeStatus::Finished, now.addDays(-1)));
        const auto list = h.trades();
        QCOMPARE(list.at(0).tradeId, QString("new"));
        QCOMPARE(list.at(1).tradeId, QString("mid"));
        QCOMPARE(list.at(2).tradeId, QString("old"));
    }

    void pollsOnlyOpenAndRecentTrades() {
        SwapHistory h;
        h.upsert(trade("open", TradeStatus::Waiting, now.addSecs(-300)));
        h.upsert(trade("halted", TradeStatus::Halted, now.addDays(-2)));
        h.upsert(trade("done", TradeStatus::Finished, now.addSecs(-300)));
        h.upsert(trade("refunded", TradeStatus::Refunded, now.addSecs(-300)));
        h.upsert(trade("forgotten", TradeStatus::Waiting, now.addDays(-15)));

        QStringList ids;
        for (const auto &t : h.tradesToPoll(now)) {
            ids << t.tradeId;
        }
        ids.sort();
        QCOMPARE(ids, QStringList({"halted", "open"}));
    }

    void unreadableEntriesAreKept() {
        // An entry written by a newer version must survive a save by this one.
        SwapHistory h;
        h.upsert(trade("A", TradeStatus::Waiting, now));
        QJsonObject root = QJsonDocument::fromJson(h.toJson()).object();
        QJsonArray trades = root.value("trades").toArray();
        trades.append(QJsonObject{{"version", 99}, {"future", true}});
        root["trades"] = trades;

        const SwapHistory loaded = SwapHistory::fromJson(QJsonDocument(root).toJson());
        QCOMPARE(loaded.size(), 1);
        const QJsonArray saved = QJsonDocument::fromJson(loaded.toJson()).object().value("trades").toArray();
        QCOMPARE(saved.size(), 2);
        QCOMPARE(saved.at(1).toObject().value("version").toInt(), 99);
    }
};

QTEST_APPLESS_MAIN(TestSwapHistory)
#include "test_swap_history.moc"
