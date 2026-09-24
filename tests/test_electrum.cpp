// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "Addresses.h"
#include "Bip39.h"
#include "CoinParams.h"
#include "Electrum.h"

using namespace biscuit::coins;
using namespace biscuit::coins::electrum;

namespace {
    const QString abandonAbout = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    const QByteArray ours0 = QByteArray::fromHex("0014c0cebcd6c3d3ca8c75dc5ec62ebe55330ef910e2");   // receive 0
    const QByteArray oursChange = QByteArray::fromHex("00143e34985dca6fddc9fb369940e4c7d8e2873f529c"); // any wallet script
    const QByteArray foreign = QByteArray::fromHex("00149c90f934ea51fa0f6504177043e0908da6929983");
}

class TestElectrum : public QObject
{
Q_OBJECT

private slots:
    void requestFormat() {
        const QByteArray line = request(7, "blockchain.scripthash.get_history", {"abcd"});
        QVERIFY(line.endsWith('\n'));
        QCOMPARE(line.count('\n'), 1);
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        QCOMPARE(obj.value("id").toInt(), 7);
        QCOMPARE(obj.value("method").toString(), QString("blockchain.scripthash.get_history"));
        QCOMPARE(obj.value("params").toArray().first().toString(), QString("abcd"));
    }

    void parseMessages() {
        auto r = parseMessage(R"({"jsonrpc":"2.0","id":3,"result":[{"tx_hash":"aa","height":5}]})");
        QVERIFY(r && r->id == 3 && r->error.isEmpty() && r->result.isArray());

        auto e = parseMessage(R"({"jsonrpc":"2.0","id":4,"error":{"code":1,"message":"bad tx"}})");
        QVERIFY(e && e->id == 4);
        QCOMPARE(e->error, QString("bad tx"));

        auto n = parseMessage(R"({"jsonrpc":"2.0","method":"blockchain.scripthash.subscribe","params":["ab","cd"]})");
        QVERIFY(n && !n->id.has_value());
        QCOMPARE(n->method, QString("blockchain.scripthash.subscribe"));
        QCOMPARE(n->params.size(), 2);

        QVERIFY(!parseMessage("garbage").has_value());
        QVERIFY(!parseMessage("[1,2]").has_value());
    }

    void lineBuffer() {
        LineBuffer b(64);
        QCOMPARE(b.feed("{\"a\":1}\n{\"b\"")->size(), 1);
        const auto second = b.feed(":2}\n\n");
        QCOMPARE(second->size(), 1);
        QCOMPARE(second->first(), QByteArray("{\"b\":2}"));
        // A server flooding a single line is rejected.
        QVERIFY(!b.feed(QByteArray(100, 'x')).has_value());
        LineBuffer c(64);
        QVERIFY(!c.feed(QByteArray(100, 'y') + "\n").has_value());
    }

    void feeConversion() {
        QCOMPARE(feeRateFromEstimate(0.0001), 10.0);        // 0.0001 coin/kB = 10 sat/vB
        QCOMPARE(feeRateFromEstimate(0.00002345), 2.345);
        QCOMPARE(feeRateFromEstimate(-1), 1.0);             // unknown -> minimum
        QCOMPARE(feeRateFromEstimate(0.000001), 1.0);       // below minimum
        QCOMPARE(feeRateFromEstimate(-1, 2.0), 2.0);
    }

    void gapScannerFreshWallet() {
        GapScanner s(5);
        const auto first = s.nextBatch();
        QCOMPARE(first.size(), 10);        // 5 receive + 5 change
        QVERIFY(!s.done());
        for (const auto &r : first) s.setUsed(r, false);
        QVERIFY(s.done());
        QVERIFY(s.nextBatch().isEmpty());
        QCOMPARE(s.firstUnused(HdAccount::Receive), 0u);
    }

    void gapScannerExtendsAfterUse() {
        GapScanner s(5);
        for (const auto &r : s.nextBatch()) {
            s.setUsed(r, r.chain == HdAccount::Receive && r.index == 3);
        }
        QVERIFY(!s.done());
        const auto more = s.nextBatch();       // receive 5..8 (up to 3 + 5)
        QCOMPARE(more.size(), 4);
        QCOMPARE(more.first().index, 5u);
        QCOMPARE(more.last().index, 8u);
        for (const auto &r : more) s.setUsed(r, false);
        QVERIFY(s.done());
        QCOMPARE(s.firstUnused(HdAccount::Receive), 4u);
        QCOMPARE(s.firstUnused(HdAccount::Change), 0u);
    }

    void parseSignedTransaction() {
        const auto acc = HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout), bitcoin());
        TxPlan plan;
        Utxo u;
        u.txid = QString("ab").repeated(32);
        u.vout = 1;
        u.value = 100000;
        plan.inputs = {u};
        plan.outputs = {{foreign, 60000}, {ours0, 38000}};
        plan.fee = 2000;
        const auto signedTx = signTransaction(plan, *acc, 0);
        QVERIFY(signedTx.has_value());

        const auto parsed = parseTransaction(signedTx->hex);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->txid, signedTx->txid);
        QCOMPARE(parsed->inputs.size(), 1);
        QCOMPARE(parsed->inputs.first().prevTxid, u.txid);
        QCOMPARE(parsed->inputs.first().prevVout, 1u);
        QCOMPARE(parsed->outputs.size(), 2);
        QCOMPARE(parsed->outputs.at(0).value, quint64(60000));
        QCOMPARE(parsed->outputs.at(1).scriptPubKey, ours0);
        QVERIFY(!parseTransaction("zz").has_value());
    }

    void historyAndUtxos() {
        // A: someone pays 100000 to us. B: we pay 60000 to someone, 38000 change back, fee 2000.
        ParsedTx a{"aa", {{"ff", 0}}, {{ours0, 100000}, {foreign, 5000}}};
        ParsedTx b{"bb", {{"aa", 0}}, {{foreign, 60000}, {oursChange, 38000}}};
        const QMap<QString, ParsedTx> txs{{"aa", a}, {"bb", b}};
        const QMap<QString, int> heights{{"aa", 800000}, {"bb", 0}};

        const auto history = computeHistory(txs, heights, {ours0, oursChange});
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.at(0).txid, QString("bb"));        // unconfirmed first
        QCOMPARE(history.at(0).delta, qint64(-62000));      // -100000 spent + 38000 change
        QCOMPARE(history.at(0).fee, std::optional<quint64>(2000));
        QCOMPARE(history.at(1).delta, qint64(100000));
        QVERIFY(!history.at(1).fee.has_value());            // inputs not ours: fee unknown

        const QMap<QByteArray, AddressRef> scripts{{ours0, {HdAccount::Receive, 0}}, {oursChange, {HdAccount::Change, 0}}};
        const auto utxos = computeUtxos(txs, heights, scripts);
        QCOMPARE(utxos.size(), 1);
        QCOMPARE(utxos.first().utxo.txid, QString("bb"));
        QCOMPARE(utxos.first().utxo.vout, 1u);
        QCOMPARE(utxos.first().utxo.value, quint64(38000));
        QCOMPARE(utxos.first().utxo.chain, HdAccount::Change);
    }
};

QTEST_APPLESS_MAIN(TestElectrum)
#include "test_electrum.moc"
