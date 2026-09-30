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
        for (const QString &url : {cryptoRatesUrl(), fiatRatesUrl(), crowdfundingUrl(), newsUrl()}) {
            QVERIFY(url.startsWith("https://"));
            QVERIFY(!url.contains("key", Qt::CaseInsensitive));
        }
        // Everything comes from Biscuit's own service: the sources never
        // see users' IP addresses.
        for (const QString &url : {cryptoRatesUrl(), fiatRatesUrl(), crowdfundingUrl(), newsUrl()}) {
            QCOMPARE(QUrl(url).host(), QString("biscuitwallet.com"));
            QVERIFY(QUrl(url).query().isEmpty());
        }
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

    // Atom test data, built from ordinary strings (moc misreads "//" inside raw strings).
    static QByteArray entry(const QString &title, const QString &href, const QString &updated,
                            const QString &summary = {}) {
        QString e = "<entry><title>" + title + "</title><link href=\"" + href + "\"/>";
        if (!updated.isEmpty()) e += "<updated>" + updated + "</updated>";
        if (!summary.isEmpty()) e += "<summary>" + summary + "</summary>";
        return (e + "</entry>").toUtf8();
    }

    static QByteArray feed(const QList<QByteArray> &entries) {
        return "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Biscuit News</title>" + entries.join() + "</feed>";
    }

    void news() {
        const QString site = "https://biscuitwallet.com";
        const QByteArray body = feed({
            entry("Hello, Biscuit", site + "/news/hello-biscuit/", "2026-09-28T00:00:00Z", "A light wallet &amp; more."),
            entry("  Version   1.0 is out ", site + "/news/v1/", "2026-11-02T00:00:00Z", "Download it now."),
            entry("Phishing", "https://biscuitwallet.com.evil.example/login", "2026-12-01T00:00:00Z"),
            entry("Credentials in the link", "https://user@biscuitwallet.com/", "2026-12-02T00:00:00Z"),
            entry("Plain http", "http://biscuitwallet.com/news/x/", "2026-12-03T00:00:00Z"),
            entry("No date", site + "/news/y/", ""),
        });
        const auto msg = newsMessage(body);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->value("cmd").toString(), QString("news"));
        const QJsonArray data = msg->value("data").toArray();
        QCOMPARE(data.size(), 2);   // only dated items linking to our own pages
        const QJsonObject newest = data.at(0).toObject();
        QCOMPARE(newest.value("title").toString(), QString("Version 1.0 is out"));
        QCOMPARE(newest.value("date").toString(), QString("2026-11-02"));
        QCOMPARE(newest.value("url").toString(), site + "/news/v1/");
        const QJsonObject older = data.at(1).toObject();
        QCOMPARE(older.value("summary").toString(), QString("A light wallet & more."));
    }

    void newsIsPlainText() {
        // Markup in a title arrives as text, never as HTML to render.
        const QByteArray body = feed({entry("&lt;b&gt;Bold&lt;/b&gt;", "https://biscuitwallet.com/news/a/", "2026-10-01T00:00:00Z")});
        const auto msg = newsMessage(body);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->value("data").toArray().at(0).toObject().value("title").toString(), QString("<b>Bold</b>"));
    }

    void rejectsGarbage() {
        QVERIFY(!cryptoRatesMessage("not json").has_value());
        QVERIFY(!cryptoRatesMessage("{}").has_value());
        QVERIFY(!cryptoRatesMessage("[]").has_value());
        QVERIFY(!fiatRatesMessage("[]").has_value());
        QVERIFY(!fiatRatesMessage(R"({"rates":{}})").has_value());
        QVERIFY(!crowdfundingMessage("oops").has_value());
        QVERIFY(!newsMessage("not xml").has_value());
        QVERIFY(!newsMessage("<html><body>502</body></html>").has_value());
        QVERIFY(!newsMessage("<feed><entry>").has_value());
    }
};

QTEST_APPLESS_MAIN(TestPublicData)
#include "test_public_data.moc"
