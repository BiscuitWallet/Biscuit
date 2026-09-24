// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <random>

#include <QtTest>

#include "Addresses.h"
#include "Bip39.h"
#include "CoinParams.h"
#include "HdAccount.h"
#include "Transactions.h"

using namespace biscuit::coins;

namespace {
    const QString abandonAbout = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

    HdAccount account() {
        return std::move(*HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout), bitcoin()));
    }

    Utxo utxo(quint64 value, quint32 index = 0, const QString &txidByte = "11") {
        Utxo u;
        u.txid = txidByte.repeated(32);
        u.vout = index;
        u.value = value;
        u.chain = HdAccount::Receive;
        u.index = index;
        u.height = 800000;
        return u;
    }

    quint64 sum(const QList<Utxo> &l) { quint64 s = 0; for (const auto &u : l) s += u.value; return s; }
    quint64 sum(const QList<TxOutput> &l) { quint64 s = 0; for (const auto &o : l) s += o.value; return s; }

    const QByteArray dest = *addressToScriptPubKey("bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g", bitcoin());
    const QByteArray change = *addressToScriptPubKey("bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el", bitcoin());
}

class TestCoinsTx : public QObject
{
Q_OBJECT

private slots:
    void vsizeEstimate() {
        // 1 P2WPKH input, 2 P2WPKH outputs: 10.5 + 68 + 2 * 31 = 140.5 -> 141 vB.
        QCOMPARE(estimateVsize(1, {22, 22}), 141);
        QCOMPARE(estimateVsize(2, {22}), 178);   // 10.5 + 2 * 68 + 31 = 177.5
    }

    void prefersSingleCoin() {
        const auto plan = planTransaction({utxo(50000, 0), utxo(200000, 1), utxo(120000, 2)}, dest, 100000, 5, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 1);
        QCOMPARE(plan->inputs.first().value, quint64(120000));   // smallest coin that covers it
        QCOMPARE(plan->amount, quint64(100000));
        QCOMPARE(plan->outputs.size(), 2);
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
    }

    void combinesLargestFirst() {
        const auto plan = planTransaction({utxo(30000, 0), utxo(60000, 1), utxo(50000, 2)}, dest, 100000, 2, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->inputs.at(0).value, quint64(60000));
        QCOMPARE(plan->inputs.at(1).value, quint64(50000));
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
    }

    void smallChangeGoesToFee() {
        // 100000 + fee(1 in, 1 out) = 100000 + 110 at 1 sat/vB; 100300 leaves < dust.
        const auto plan = planTransaction({utxo(100300)}, dest, 100000, 1, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->outputs.size(), 1);
        QCOMPARE(plan->changeOutput, -1);
        QCOMPARE(plan->fee, quint64(300));
    }

    void sendAll() {
        const QList<Utxo> coins = {utxo(40000, 0), utxo(60000, 1)};
        const auto plan = planTransaction(coins, dest, 0, 3, change, true);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->outputs.size(), 1);
        QCOMPARE(plan->amount + plan->fee, quint64(100000));
        QCOMPARE(plan->fee, quint64(std::ceil(estimateVsize(2, {22}) * 3.0)));
    }

    void refusals() {
        QString error;
        QVERIFY(!planTransaction({utxo(1000)}, dest, 5000, 1, change, false, &error));
        QVERIFY(error.contains("Insufficient"));
        QVERIFY(!planTransaction({utxo(100000)}, dest, 100, 1, change, false, &error));
        QVERIFY(error.contains("too small"));
        QVERIFY(!planTransaction({utxo(100000)}, dest, 5000, 0, change, false, &error));
        QVERIFY(!planTransaction({utxo(100000)}, dest, 5000, std::nan(""), change, false, &error));
        QVERIFY(!planTransaction({}, dest, 5000, 1, change, false, &error));
        QVERIFY(!planTransaction({utxo(600)}, dest, 0, 10, change, true, &error));
        QVERIFY(!planTransaction({utxo(100000)}, {}, 5000, 1, change, false, &error));
    }

    void conservationAndFeeRateProperty() {
        // Random scenarios: inputs = outputs + fee, fee rate respected, no dust.
        std::mt19937_64 rng(7);
        for (int round = 0; round < 2000; ++round) {
            QList<Utxo> coins;
            const int n = 1 + int(rng() % 8);
            for (int i = 0; i < n; ++i) coins.append(utxo(1000 + rng() % 2000000, i));
            const double rate = 1 + double(rng() % 200);
            const bool all = rng() % 5 == 0;
            const quint64 amount = 546 + rng() % 3000000;

            const auto plan = planTransaction(coins, dest, amount, rate, change, all);
            if (!plan) continue;
            QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
            QVERIFY(plan->fee >= quint64(std::ceil(plan->estimatedVsize * rate)));
            for (const auto &o : plan->outputs) QVERIFY(o.value >= dustLimit);
            if (!all) QCOMPARE(plan->amount, amount);
        }
    }

    void signsValidTransaction() {
        const HdAccount acc = account();
        TxPlan plan;
        plan.inputs = {utxo(150000, 0, "ab"), utxo(80000, 1, "cd")};
        plan.outputs = {{dest, 200000}, {change, 28000}};
        plan.amount = 200000;
        plan.fee = 2000;

        QString error;
        const auto tx = signTransaction(plan, acc, 850000, &error);
        QVERIFY2(tx.has_value(), qPrintable(error));
        QVERIFY(tx->hex.startsWith("020000000001"));        // version 2 + segwit marker/flag
        QVERIFY(tx->hex.endsWith("50f80c00"));              // locktime 850000 = 0x0cf850, little-endian
        QCOMPARE(tx->txid.size(), 64);
        QVERIFY(tx->vsize <= estimateVsize(2, {22, 22}));   // estimate is an upper bound
        QVERIFY(tx->vsize >= estimateVsize(2, {22, 22}) - 2);
        QVERIFY(tx->hex.contains("fdffffff"));              // replace-by-fee signaled

        // Deterministic signatures (RFC 6979): same plan, same transaction.
        QCOMPARE(signTransaction(plan, acc, 850000)->hex, tx->hex);
    }

    void refusesToSignForeignOrBrokenInput() {
        const HdAccount acc = account();
        TxPlan plan;
        plan.inputs = {utxo(150000)};
        plan.inputs[0].txid = "not-hex";
        plan.outputs = {{dest, 100000}};
        QVERIFY(!signTransaction(plan, acc, 0).has_value());
        QVERIFY(!signTransaction(TxPlan{}, acc, 0).has_value());
    }
};

QTEST_APPLESS_MAIN(TestCoinsTx)
#include "test_coins_tx.moc"
