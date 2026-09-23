// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QJsonDocument>
#include <QtTest>

#include "SwapTypes.h"

using namespace biscuit::swap;

class TestTradeStatus : public QObject
{
Q_OBJECT

private slots:
    void parsesAllDocumentedStatuses() {
        const QList<QPair<QString, TradeStatus>> cases = {
            {"new", TradeStatus::New},
            {"waiting", TradeStatus::Waiting},
            {"confirming", TradeStatus::Confirming},
            {"sending", TradeStatus::Sending},
            {"paid partially", TradeStatus::PaidPartially},
            {"finished", TradeStatus::Finished},
            {"failed", TradeStatus::Failed},
            {"expired", TradeStatus::Expired},
            {"halted", TradeStatus::Halted},
            {"refunded", TradeStatus::Refunded},
        };
        for (const auto &[name, status] : cases) {
            QCOMPARE(tradeStatusFromString(name), status);
            QCOMPARE(tradeStatusToString(status), name);
        }
        QCOMPARE(tradeStatusFromString(" Paid_Partially "), TradeStatus::PaidPartially);
        QCOMPARE(tradeStatusFromString("something else"), TradeStatus::Unknown);
    }

    void terminalAndSupport() {
        QVERIFY(isTerminal(TradeStatus::Finished));
        QVERIFY(isTerminal(TradeStatus::Refunded));
        QVERIFY(!isTerminal(TradeStatus::Expired));
        QVERIFY(!isTerminal(TradeStatus::Halted));
        QVERIFY(needsSupport(TradeStatus::Failed));
        QVERIFY(needsSupport(TradeStatus::Halted));
        QVERIFY(!needsSupport(TradeStatus::Finished));
    }

    void normalPath() {
        Trade t;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (auto s : {TradeStatus::Waiting, TradeStatus::Confirming, TradeStatus::Sending, TradeStatus::Finished}) {
            QVERIFY(applyStatus(t, s, now));
            QCOMPARE(t.status, s);
        }
    }

    void staleUpdatesDoNotMoveBack() {
        QVERIFY(!canTransition(TradeStatus::Sending, TradeStatus::Waiting));
        QVERIFY(!canTransition(TradeStatus::Confirming, TradeStatus::New));
        QVERIFY(!canTransition(TradeStatus::Waiting, TradeStatus::Waiting));
        QVERIFY(!canTransition(TradeStatus::Waiting, TradeStatus::Unknown));
    }

    void terminalStatesAreFinal() {
        for (auto s : {TradeStatus::Waiting, TradeStatus::Failed, TradeStatus::Refunded, TradeStatus::Sending}) {
            QVERIFY(!canTransition(TradeStatus::Finished, s));
        }
        QVERIFY(!canTransition(TradeStatus::Refunded, TradeStatus::Finished));
    }

    void sideStates() {
        QVERIFY(canTransition(TradeStatus::Waiting, TradeStatus::Expired));
        QVERIFY(canTransition(TradeStatus::Confirming, TradeStatus::Halted));
        QVERIFY(canTransition(TradeStatus::Sending, TradeStatus::Failed));
        // Late deposit after expiry, support resolving a halted trade.
        QVERIFY(canTransition(TradeStatus::Expired, TradeStatus::Confirming));
        QVERIFY(canTransition(TradeStatus::Halted, TradeStatus::Refunded));
        QVERIFY(canTransition(TradeStatus::Halted, TradeStatus::Finished));
        QVERIFY(canTransition(TradeStatus::PaidPartially, TradeStatus::Confirming));
    }

    void jsonRoundTrip() {
        Trade t;
        t.providerId = "trocador";
        t.tradeId = "ABC123";
        t.exchange = "ChangeNOW";
        t.exchangeTradeId = "xyz";
        t.exchangePassword = "secret";
        t.from = {"xmr", "Mainnet"};
        t.to = {"btc", "Mainnet"};
        t.amountFrom = "1.5";
        t.amountTo = "0.00631";
        t.rateType = RateType::Fixed;
        t.kycRating = KycRating::B;
        t.depositAddress = "4deposit";
        t.depositMemo = "memo";
        t.payoutAddress = "bc1payout";
        t.refundAddress = "4refund";
        t.status = TradeStatus::Confirming;
        t.createdAt = QDateTime::fromString("2026-09-23T10:00:00Z", Qt::ISODate);
        t.updatedAt = QDateTime::fromString("2026-09-23T10:05:00Z", Qt::ISODate);
        t.depositTxId = "abcdef";

        const QByteArray bytes = QJsonDocument(t.toJson()).toJson();
        const auto back = Trade::fromJson(QJsonDocument::fromJson(bytes).object());
        QVERIFY(back.has_value());
        QCOMPARE(back->toJson(), t.toJson());
        QCOMPARE(back->exchangePassword, QString("secret"));
        QCOMPARE(back->status, TradeStatus::Confirming);
        QCOMPARE(back->createdAt, t.createdAt);
    }

    void jsonRejectsIncompleteOrUnknownVersion() {
        QVERIFY(!Trade::fromJson(QJsonObject{}).has_value());
        QVERIFY(!Trade::fromJson(QJsonObject{{"version", 2}, {"trade_id", "x"}}).has_value());
    }
};

QTEST_APPLESS_MAIN(TestTradeStatus)
#include "test_trade_status.moc"
