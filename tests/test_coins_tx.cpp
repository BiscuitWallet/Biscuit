// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <random>

#include <QtTest>

#include "Addresses.h"
#include "Bip39.h"
#include "CoinParams.h"
#include "HdAccount.h"
#include "Transactions.h"

#include <wally_crypto.h>
#include <wally_transaction.h>

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
        QCOMPARE(estimateVsize(bitcoin(), 1, {22, 22}), 141);
        QCOMPARE(estimateVsize(bitcoin(), 2, {22}), 178);   // 10.5 + 2 * 68 + 31 = 177.5
    }

    void prefersSingleCoin() {
        const auto plan = planTransaction(bitcoin(), {utxo(50000, 0), utxo(200000, 1), utxo(120000, 2)}, dest, 100000, 5, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 1);
        QCOMPARE(plan->inputs.first().value, quint64(120000));   // smallest coin that covers it
        QCOMPARE(plan->amount, quint64(100000));
        QCOMPARE(plan->outputs.size(), 2);
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
    }

    void combinesLargestFirst() {
        const auto plan = planTransaction(bitcoin(), {utxo(30000, 0), utxo(60000, 1), utxo(50000, 2)}, dest, 100000, 2, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->inputs.at(0).value, quint64(60000));
        QCOMPARE(plan->inputs.at(1).value, quint64(50000));
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
    }

    void smallChangeGoesToFee() {
        // 100000 + fee(1 in, 1 out) = 100000 + 110 at 1 sat/vB; 100300 leaves < dust.
        const auto plan = planTransaction(bitcoin(), {utxo(100300)}, dest, 100000, 1, change, false);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->outputs.size(), 1);
        QCOMPARE(plan->changeOutput, -1);
        QCOMPARE(plan->fee, quint64(300));
    }

    void sendAll() {
        const QList<Utxo> coins = {utxo(40000, 0), utxo(60000, 1)};
        const auto plan = planTransaction(bitcoin(), coins, dest, 0, 3, change, true);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->outputs.size(), 1);
        QCOMPARE(plan->amount + plan->fee, quint64(100000));
        QCOMPARE(plan->fee, quint64(std::ceil(estimateVsize(bitcoin(), 2, {22}) * 3.0)));
    }

    void refusals() {
        QString error;
        QVERIFY(!planTransaction(bitcoin(), {utxo(1000)}, dest, 5000, 1, change, false, &error));
        QVERIFY(error.contains("Insufficient"));
        QVERIFY(!planTransaction(bitcoin(), {utxo(100000)}, dest, 100, 1, change, false, &error));
        QVERIFY(error.contains("too small"));
        QVERIFY(!planTransaction(bitcoin(), {utxo(100000)}, dest, 5000, 0, change, false, &error));
        QVERIFY(!planTransaction(bitcoin(), {utxo(100000)}, dest, 5000, std::nan(""), change, false, &error));
        QVERIFY(!planTransaction(bitcoin(), {}, dest, 5000, 1, change, false, &error));
        QVERIFY(!planTransaction(bitcoin(), {utxo(600)}, dest, 0, 10, change, true, &error));
        QVERIFY(!planTransaction(bitcoin(), {utxo(100000)}, {}, 5000, 1, change, false, &error));
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

            const auto plan = planTransaction(bitcoin(), coins, dest, amount, rate, change, all);
            if (!plan) continue;
            QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);
            QVERIFY(plan->fee >= quint64(std::ceil(plan->estimatedVsize * rate)));
            for (const auto &o : plan->outputs) QVERIFY(o.value >= bitcoin().dustLimit);
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
        QVERIFY(tx->vsize <= estimateVsize(bitcoin(), 2, {22, 22}));   // estimate is an upper bound
        QVERIFY(tx->vsize >= estimateVsize(bitcoin(), 2, {22, 22}) - 2);
        QVERIFY(tx->hex.contains("fdffffff"));              // replace-by-fee signaled

        // Deterministic signatures (RFC 6979): same plan, same transaction.
        QCOMPARE(signTransaction(plan, acc, 850000)->hex, tx->hex);
    }

    void signsLegacyTransaction() {
        // Dogecoin: P2PKH inputs, version 1, no SegWit, no replace-by-fee.
        const HdAccount acc = std::move(*HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout), dogecoin()));
        TxPlan plan;
        plan.inputs = {utxo(500000000, 0, "ab"), utxo(300000000, 1, "cd")};
        plan.outputs = {{acc.scriptPubKey(HdAccount::Receive, 5), 600000000},
                        {acc.scriptPubKey(HdAccount::Change, 0), 199000000}};
        plan.fee = 1000000;

        QString error;
        const auto tx = signTransaction(plan, acc, 6400000, &error);
        QVERIFY2(tx.has_value(), qPrintable(error));
        QVERIFY(tx->hex.startsWith("0100000002"));          // version 1, two inputs: no segwit marker
        QVERIFY(tx->hex.contains("feffffff"));
        QVERIFY(!tx->hex.contains("fdffffff"));             // no replace-by-fee
        QVERIFY(tx->vsize <= estimateVsize(dogecoin(), 2, {25, 25}));
        QVERIFY(tx->vsize >= estimateVsize(dogecoin(), 2, {25, 25}) - 4);
        QCOMPARE(transactionVsize(tx->hex), tx->vsize);

        // Each scriptSig is <signature> <key>, and each signature verifies
        // against the legacy hash of the finished transaction.
        wally_tx *raw = nullptr;
        QCOMPARE(wally_tx_from_hex(tx->hex.toLatin1().constData(), 0, &raw), WALLY_OK);
        for (size_t i = 0; i < raw->num_inputs; ++i) {
            const wally_tx_input &in = raw->inputs[i];
            const QByteArray script(reinterpret_cast<const char *>(in.script), qsizetype(in.script_len));
            const int sigLen = quint8(script.at(0));
            const QByteArray der = script.mid(1, sigLen - 1);   // without the SIGHASH_ALL byte
            QCOMPARE(quint8(script.at(sigLen)), quint8(WALLY_SIGHASH_ALL));
            const QByteArray pub = script.mid(1 + sigLen + 1);
            QCOMPARE(pub, acc.publicKey(HdAccount::Receive, quint32(i)));
            const QByteArray prev = acc.scriptPubKey(HdAccount::Receive, quint32(i));
            unsigned char hash[SHA256_LEN];
            unsigned char sig[EC_SIGNATURE_LEN];
            QCOMPARE(wally_tx_get_btc_signature_hash(raw, i, reinterpret_cast<const unsigned char *>(prev.constData()),
                                                     prev.size(), 0, WALLY_SIGHASH_ALL, 0, hash, sizeof(hash)), WALLY_OK);
            QCOMPARE(wally_ec_sig_from_der(reinterpret_cast<const unsigned char *>(der.constData()), der.size(),
                                           sig, sizeof(sig)), WALLY_OK);
            QCOMPARE(wally_ec_sig_verify(reinterpret_cast<const unsigned char *>(pub.constData()), pub.size(),
                                         hash, sizeof(hash), EC_FLAG_ECDSA, sig, sizeof(sig)), WALLY_OK);
        }
        wally_tx_free(raw);
    }

    void legacyPlanning() {
        const HdAccount acc = std::move(*HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout), dogecoin()));
        const QByteArray to = acc.scriptPubKey(HdAccount::Receive, 5);
        const QByteArray back = acc.scriptPubKey(HdAccount::Change, 0);
        // 10 + 148 + 2 × 34 bytes.
        QCOMPARE(estimateVsize(dogecoin(), 1, {25, 25}), 226);
        QCOMPARE(inputCost(dogecoin(), 50000), quint64(7400000));   // 0.074 DOGE at 0.5 DOGE/kB

        const auto plan = planTransaction(dogecoin(), {utxo(500000000)}, to, 100000000, 50000, back, false);
        QVERIFY(plan);
        QCOMPARE(plan->fee, quint64(226 * 50000));
        QCOMPARE(sum(plan->outputs) + plan->fee, quint64(500000000));

        QString error;
        // Below 0.01 DOGE, below Dogecoin's minimum fee rate.
        QVERIFY(!planTransaction(dogecoin(), {utxo(500000000)}, to, 999999, 50000, back, false, &error));
        QCOMPARE(error, QString("Amount too small (minimum 0.01 DOGE)"));
        QVERIFY(!planTransaction(dogecoin(), {utxo(500000000)}, to, 100000000, 999, back, false, &error));
        // Change under 0.01 DOGE goes to the fee.
        const auto noChange = planTransaction(dogecoin(), {utxo(100000000 + 226 * 50000 + 500000)}, to, 100000000, 50000, back, false);
        QVERIFY(noChange);
        QCOMPARE(noChange->outputs.size(), 1);
        // No speed up.
        QVERIFY(!planFeeBump(dogecoin(), {utxo(500000000)}, {{to, 100000000}, {back, 300000000}}, 1, 11300000, 100000, {}, back));
    }

    void feeBump() {
        const QList<Utxo> inputs = {utxo(150000)};
        const QList<TxOutput> outputs = {{dest, 100000}, {change, 48500}};   // fee 1500
        const quint64 originalFee = 1500;

        // The change pays the extra fee; inputs and payment are unchanged.
        auto plan = planFeeBump(bitcoin(), inputs, outputs, 1, originalFee, 20, {}, change);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 1);
        QCOMPARE(plan->outputs.size(), 2);
        QCOMPARE(plan->outputs[0].scriptPubKey, dest);
        QCOMPARE(plan->outputs[0].value, quint64(100000));
        QCOMPARE(plan->changeOutput, 1);
        QVERIFY(plan->fee >= quint64(20 * plan->estimatedVsize));
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);

        // Relay rule: at least the old fee + 1 sat/vB, even at a low rate.
        plan = planFeeBump(bitcoin(), inputs, outputs, 1, originalFee, 1, {}, change);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->fee, originalFee + quint64(plan->estimatedVsize));

        // Change a bit short: it goes to the fee rather than adding a coin
        // (cheaper, and no new link between coins).
        const QList<TxOutput> tight = {{dest, 100000}, {change, 49000}};   // fee 1000
        plan = planFeeBump(bitcoin(), {utxo(150000)}, tight, 1, 1000, 400, {utxo(90000, 2, "33")}, change);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 1);
        QCOMPARE(plan->outputs.size(), 1);
        QCOMPARE(plan->fee, quint64(50000));

        // Far too small: another coin comes in, with change.
        plan = planFeeBump(bitcoin(), {utxo(150000)}, tight, 1, 1000, 500, {utxo(30000, 1, "22"), utxo(90000, 2, "33")}, change);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->inputs[1].value, quint64(90000));   // the largest first
        QCOMPARE(plan->outputs[0].value, quint64(100000));
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);

        // ...and without another coin, it says so.
        QString error;
        QVERIFY(!planFeeBump(bitcoin(), {utxo(150000)}, tight, 1, 1000, 500, {}, change, &error));
        QVERIFY(error.contains("Not enough"));

        // No change output: a coin is added, with new change.
        plan = planFeeBump(bitcoin(), {utxo(101500)}, {{dest, 100000}}, -1, 1500, 10, {utxo(50000, 1, "44")}, change);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->inputs.size(), 2);
        QCOMPARE(plan->outputs.size(), 2);
        QCOMPARE(plan->outputs[plan->changeOutput].scriptPubKey, change);
        QCOMPARE(sum(plan->inputs), sum(plan->outputs) + plan->fee);

        // The size of a real signed transaction reads back.
        const HdAccount acc = account();
        const auto tx = signTransaction(*planFeeBump(bitcoin(), inputs, outputs, 1, originalFee, 20, {}, change), acc, 850000);
        QVERIFY(tx.has_value());
        QCOMPARE(transactionVsize(tx->hex), tx->vsize);
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

    void dust() {
        // A native SegWit input is 68 vB: at 5 sat/vB it costs 340 sats.
        QCOMPARE(inputCost(bitcoin(), 5.0), quint64(340));
        QCOMPARE(inputCost(bitcoin(), 1.0), quint64(68));
        QCOMPARE(inputCost(bitcoin(), 1.5), quint64(102));
        QVERIFY(isDust(bitcoin(), 340, 5.0));      // worth exactly its cost: nothing gained
        QVERIFY(!isDust(bitcoin(), 341, 5.0));
        QVERIFY(isDust(bitcoin(), 546, 10.0));     // the dust limit itself, on a busy day
        QVERIFY(!isDust(bitcoin(), 546, 1.0));
    }
};

QTEST_APPLESS_MAIN(TestCoinsTx)
#include "test_coins_tx.moc"
