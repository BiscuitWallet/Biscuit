// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Parsing of the biscuit-swapd event stream. Sample lines come from a real run.

#include <QtTest>

#include "AtomicEvents.h"

using namespace biscuit::swap::atomic;

class TestAtomicEvents : public QObject
{
Q_OBJECT

private slots:
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
