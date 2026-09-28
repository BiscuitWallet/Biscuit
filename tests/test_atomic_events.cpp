// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Parsing of the biscuit-swapd event stream. Sample lines come from a real run.

#include <QtTest>

#include "AtomicEvents.h"
#include "AtomicSwapRecord.h"

using namespace biscuit::swap::atomic;

class TestAtomicEvents : public QObject
{
Q_OBJECT

private slots:
    void swapEvents() {
        Event e = parseLine(R"({"type":"swap_started","swap_id":"ea030832-3be9-454f-bb98-5ea9a788406b","btc_amount_sat":300000,"lock_fee_sat":1420,"elapsed_ms":9000})");
        QCOMPARE(e.type, Event::Type::SwapStarted);
        QCOMPARE(e.swapId, QString("ea030832-3be9-454f-bb98-5ea9a788406b"));
        QCOMPARE(e.btcAmountSat, quint64(300000));
        QCOMPARE(e.lockFeeSat, quint64(1420));

        e = parseLine(R"({"type":"swap_state","swap_id":"x","stage":"btc_locked","state":"btc is locked"})");
        QCOMPARE(e.type, Event::Type::SwapState);
        QCOMPARE(e.stage, QString("btc_locked"));
        QCOMPARE(e.stateText, QString("btc is locked"));

        e = parseLine(R"({"type":"swap_finished","swap_id":"x","stage":"done"})");
        QCOMPARE(e.type, Event::Type::SwapFinished);
        QCOMPARE(e.stage, QString("done"));

        e = parseLine(R"({"type":"bitcoin","status":"ready","balance_sat":512345})");
        QCOMPARE(e.type, Event::Type::Bitcoin);
        QCOMPARE(e.bitcoinStatus, QString("ready"));
        QCOMPARE(e.balanceSat, quint64(512345));
    }

    void stages() {
        QVERIFY(!isFinalStage(stage::setup));
        QVERIFY(!fundsAtStake(stage::setup));      // nothing left the wallet yet
        QVERIFY(fundsAtStake(stage::btcLocked));
        QVERIFY(fundsAtStake(stage::refunding));
        for (const QString &s : {stage::done, stage::refunded, stage::punished, stage::cancelled}) {
            QVERIFY(isFinalStage(s));
            QVERIFY(!fundsAtStake(s));
        }
    }

    void clearHistoryKeepsUnfinishedSwaps() {
        auto record = [](const QString &id, const QString &stage) {
            AtomicSwapRecord r;
            r.id = id;
            r.stage = stage;
            return r;
        };
        const QList<AtomicSwapRecord> records = {
            record("done", stage::done), record("refunded", stage::refunded), record("punished", stage::punished),
            record("cancelled", stage::cancelled), record("setup", stage::setup), record("locked", stage::btcLocked),
            record("refunding", stage::refunding), record("redeeming", stage::redeeming),
        };
        QStringList kept;
        for (const auto &r : withoutFinished(records, {})) kept << r.id;
        QCOMPARE(kept, QStringList({"setup", "locked", "refunding", "redeeming"}));

        // The swap the helper is running stays, whatever its last stage.
        kept.clear();
        for (const auto &r : withoutFinished(records, "done")) kept << r.id;
        QVERIFY(kept.contains("done"));
        QCOMPARE(withoutFinished({}, {}).size(), 0);
    }

    void failureReasons() {
        QCOMPARE(failureReason(""), QString());
        // Real message from a maker whose offer said 0.000001 BTC.
        QCOMPARE(failureReason("Swap failed: A network error occurred while setting up the swap: Something went wrong during the swap setup protocol: "
                               "Seller refused to buy 0.00012802 BTC because the minimum configured buy limit is 0.01000000 BTC"),
                 QString("The maker refused: its real minimum is 0.01 BTC"));
        QCOMPARE(failureReason("Seller refused to buy 2.00000000 BTC because the maximum configured buy limit is 1.50000000 BTC"),
                 QString("The maker refused: its maximum right now is 1.5 BTC"));
        QCOMPARE(failureReason("Something went wrong during the swap setup protocol: Seller encountered a problem, please try again later."),
                 QString("The maker could not give a live price right now"));
        QCOMPARE(failureReason("Seller's XMR balance is currently too low to fulfill the swap request to buy 0.00010000 BTC, please try again later"),
                 QString("The maker does not have enough XMR right now"));
        QCOMPARE(failureReason("Swap rejected: anti-spam deposit too small"),
                 QString("The maker rejected the swap: anti-spam deposit too small"));
        QCOMPARE(failureReason("Not enough BTC: 0.0001 BTC + 0.00001 BTC network fee needed"),
                 QString("Not enough BTC: 0.0001 BTC + 0.00001 BTC network fee needed"));
    }

    void records() {
        AtomicSwapRecord older;
        older.id = "a";
        older.created = QDateTime(QDate(2026, 9, 26), QTime(10, 0), QTimeZone::UTC);
        AtomicSwapRecord newer;
        newer.id = "b";
        newer.walletId = "main-BTC";
        newer.makerHost = "l7attamg....onion";
        newer.priceSatPerXmr = 664750;
        newer.btcSat = 300000;
        newer.lockFeeSat = 1420;
        newer.xmrAddress = "8AbC";
        newer.tor = true;
        newer.stage = stage::xmrLocked;
        newer.created = QDateTime(QDate(2026, 9, 27), QTime(10, 0), QTimeZone::UTC);

        const auto back = recordsFromJson(recordsToJson({older, newer}));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).id, QString("b"));    // newest first
        QCOMPARE(back.at(0).walletId, QString("main-BTC"));
        QCOMPARE(back.at(0).btcSat, quint64(300000));
        QCOMPARE(back.at(0).lockFeeSat, quint64(1420));
        QCOMPARE(back.at(0).stage, stage::xmrLocked);
        QVERIFY(back.at(0).tor);
        QCOMPARE(back.at(0).created, newer.created);
        // 0.003 BTC at 0.0066475 BTC/XMR = 0.451297480... XMR
        QCOMPARE(back.at(0).expectedXmrAtomic(), quint64(451297480255));
        QCOMPARE(recordsFromJson("not json").size(), 0);
    }

    void summary() {
        const Event e = parseLine(R"({"connected":6,"dialing":2,"makers_known":35,"offers":6,"quotes_inflight":0,"type":"summary"})");
        QCOMPARE(e.type, Event::Type::Summary);
        QCOMPARE(e.summary.makersKnown, 35);
        QCOMPARE(e.summary.dialing, 2);
        QCOMPARE(e.summary.connected, 6);
        QCOMPARE(e.summary.offers, 6);
        QCOMPARE(discoveryHeadline(e.summary), QString("Dialing peers..."));
        QVERIFY(discoveryActive(e.summary));
    }

    void headlinePriority() {
        DiscoverySummary s;
        QCOMPARE(discoveryHeadline(s), QString("Waiting a few seconds..."));
        QVERIFY(!discoveryActive(s));
        s.dialing = 1;
        s.quotesInflight = 1;
        // Offers being fetched win over dialing, as in eigenwallet.
        QCOMPARE(discoveryHeadline(s), QString("Getting offers..."));
    }

    void offers() {
        const Event e = parseLine(R"({"quotes":[)"
            R"({"address":"/dns4/pulse.mikaswap.com/tcp/443/wss/p2p/12D3KooWJ4CxRWEHdZBLCQ3UL75PGBX7GZo9AFSnVzLDWEz8nYAu","max_sat":3500000,"min_sat":5000,"peer_id":"12D3KooWJ4CxRWEHdZBLCQ3UL75PGBX7GZo9AFSnVzLDWEz8nYAu","price_sat_per_xmr":852854,"refund_policy":{"content":{"anti_spam_deposit_ratio":0.015},"type":"PartialRefund"},"version":"4.15.0"},)"
            R"({"address":"/onion3/7o3o62luxrnw5o3s6cu6bixskvsi6cezgxp5j7y3pjaerttd7luysqyd:9939/p2p/12D3KooWEBfWg57cygrDXqqok4SFxyqgmgvHLhwyVtTH96QPcZQX","max_sat":0,"min_sat":0,"peer_id":"12D3KooWEBfWg57cygrDXqqok4SFxyqgmgvHLhwyVtTH96QPcZQX","price_sat_per_xmr":0,"refund_policy":{"type":"FullRefund"},"version":"4.14.0"})"
            R"(],"type":"quotes"})");
        QCOMPARE(e.type, Event::Type::Offers);
        QCOMPARE(e.offers.size(), 2);

        const MakerOffer &mika = e.offers[0];
        QVERIFY(mika.available());
        QCOMPARE(mika.host(), QString("pulse.mikaswap.com"));
        QCOMPARE(formatBtc(mika.priceSatPerXmr), QString("0.00852854"));
        QCOMPARE(formatBtc(mika.minSat), QString("0.00005"));
        QCOMPARE(formatBtc(mika.maxSat), QString("0.035"));
        QCOMPARE(formatDeposit(mika.refundDeposit), QString("1.5%"));
        QCOMPARE(mika.version, QString("4.15.0"));

        const MakerOffer &empty = e.offers[1];
        QVERIFY(!empty.available());
        QCOMPARE(empty.host(), QString("7o3o62lu….onion"));
        QCOMPARE(formatDeposit(empty.refundDeposit), QString("none"));
    }

    void torAndLifecycle() {
        QCOMPARE(parseLine(R"({"status":"bootstrapping","type":"tor"})").torStatus, QString("bootstrapping"));
        const Event started = parseLine(R"({"rendezvous_points":["12D3KooWGRvf7qVQDrNR5nfYD6rKrbgeTi9x8RrbdxbmsPvxL4mw"],"tor":true,"type":"started"})");
        QCOMPARE(started.type, Event::Type::Started);
        QVERIFY(started.usesTor);
        const Event error = parseLine(R"({"type":"error","message":"Failed to bootstrap Tor"})");
        QCOMPARE(error.type, Event::Type::Error);
        QCOMPARE(error.message, QString("Failed to bootstrap Tor"));
        QCOMPARE(parseLine(R"({"type":"stopped"})").type, Event::Type::Stopped);
    }

    void ignoredAndInvalid() {
        QCOMPARE(parseLine(R"({"peer_id":"x","rendezvous":false,"status":"dialing","type":"peer"})").type, Event::Type::Ignored);
        QCOMPARE(parseLine("2026-09-25T15:37:10Z ERROR something").type, Event::Type::Invalid);
        QCOMPARE(parseLine("").type, Event::Type::Invalid);
    }

    void marketDeviation_() {
        // Market 0.0068 BTC/XMR (seen on 2026-09-25).
        QVERIFY(!marketDeviation(852854, 0).has_value());
        QVERIFY(!marketDeviation(0, 0.0068).has_value());

        const double overpriced = *marketDeviation(852854, 0.0068);
        QVERIFY(overpriced > 25.4 && overpriced < 25.5);
        QCOMPARE(formatDeviation(overpriced), QString("+25%"));
        QVERIFY(deviationNeedsWarning(overpriced));

        const double close = *marketDeviation(679380, 0.0068);
        QCOMPARE(formatDeviation(close), QString("-0.1%"));
        QVERIFY(!deviationNeedsWarning(close));

        QCOMPARE(formatDeviation(4.96), QString("+5.0%"));
        QVERIFY(deviationNeedsWarning(5.0));
        QVERIFY(!deviationNeedsWarning(-9.9));
        QVERIFY(deviationNeedsWarning(-10.0));
        QCOMPARE(formatDeviation(0.04), QString("0%"));
    }

    void unavailableWhenMinAboveMax() {
        MakerOffer o;
        o.priceSatPerXmr = 800000;
        o.minSat = 10;
        o.maxSat = 5;
        QVERIFY(!o.available());
    }
};

QTEST_MAIN(TestAtomicEvents)
#include "test_atomic_events.moc"
