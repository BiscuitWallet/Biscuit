// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QJsonArray>
#include <QUrlQuery>
#include <QtTest>

#include "PublicData.h"

using namespace biscuit::datafeed;

class TestPublicData : public QObject
{
Q_OBJECT

private slots:
    void urlsAreFixedAndAnonymous() {
        // Same request for every user: no key, no personal parameter.
        for (const QString &url : {cryptoRatesUrl(), fiatRatesUrl(), crowdfundingUrl()}) {
            QVERIFY(url.startsWith("https://"));
            QVERIFY(!url.contains("key", Qt::CaseInsensitive));
        }
        QCOMPARE(cryptoRatesUrl(), cryptoRatesUrl());
        const QUrlQuery q(QUrl(cryptoRatesUrl()).query());
        QCOMPARE(q.queryItemValue("vs_currency"), QString("usd"));
        QVERIFY(q.queryItemValue("ids").split(',').contains("monero"));
    }

    void cryptoRates() {
        const QByteArray body = R"([
            {"id":"monero","symbol":"xmr","name":"Monero","image":"img/xmr.png","current_price":544.27,"price_change_percentage_24h":1.5},
            {"id":"bitcoin","symbol":"btc","name":"Bitcoin","image":"img/btc.png","current_price":83531,"price_change_percentage_24h":-0.4},
            {"id":"dead","symbol":"dead","name":"Dead","current_price":0}
        ])";
        const auto msg = cryptoRatesMessage(body);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->value("cmd").toString(), QString("crypto_rates"));
        const QJsonArray data = msg->value("data").toArray();
        QCOMPARE(data.size(), 2);
        QCOMPARE(data.at(0).toObject().value("symbol").toString(), QString("xmr"));
        QCOMPARE(data.at(0).toObject().value("current_price").toDouble(), 544.27);
        QVERIFY(!data.at(0).toObject().contains("image"));
    }

    void fiatRates() {
        const QByteArray body = R"({"amount":1.0,"base":"USD","date":"2026-09-23","rates":{"EUR":0.87635,"GBP":0.75322,"BAD":0}})";
        const auto msg = fiatRatesMessage(body);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->value("cmd").toString(), QString("fiat_rates"));
        const QJsonObject rates = msg->value("data").toObject().value("rates").toObject();
        QCOMPARE(rates.value("EUR").toDouble(), 0.87635);
        QCOMPARE(rates.value("USD").toDouble(), 1.0);
        QVERIFY(!rates.contains("BAD"));
    }

    void crowdfunding() {
        const QByteArray body = R"({"data":[
            {"address":null,"author":"a","state":"COMPLETED","title":"Old","target_amount":100},
            {"address":"8abc","author":"b","state":"FUNDING-REQUIRED","title":"New","target_amount":50,"raised_amount":10,"percentage_funded":20,"contributions":3,"date":"May 1, 2026"}
        ]})";
        const auto msg = crowdfundingMessage(body);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->value("cmd").toString(), QString("ccs"));
        const QJsonArray data = msg->value("data").toArray();
        QCOMPARE(data.size(), 1);
        const QJsonObject p = data.at(0).toObject();
        QCOMPARE(p.value("title").toString(), QString("New"));
        QCOMPARE(p.value("organizer").toString(), QString("CCS"));
        QCOMPARE(p.value("currency").toString(), QString("XMR"));
    }

    void rejectsGarbage() {
        QVERIFY(!cryptoRatesMessage("not json").has_value());
        QVERIFY(!cryptoRatesMessage("{}").has_value());
        QVERIFY(!cryptoRatesMessage("[]").has_value());
        QVERIFY(!fiatRatesMessage("[]").has_value());
        QVERIFY(!fiatRatesMessage(R"({"rates":{}})").has_value());
        QVERIFY(!crowdfundingMessage("oops").has_value());
    }
};

QTEST_APPLESS_MAIN(TestPublicData)
#include "test_public_data.moc"
