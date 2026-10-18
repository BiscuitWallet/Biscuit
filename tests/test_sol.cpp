// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Solana keys, addresses, amounts and SOL transfers, checked against the
// SLIP-10 vectors, the address Phantom gives for the "abandon … about" seed
// (both computed again with Node's own crypto), and replies of the public
// nodes (2026-10-18).

#include <QJsonDocument>
#include <QtTest>

#include "Bip39.h"
#include "Sol.h"
#include "SolApi.h"

using namespace biscuit::coins;
using namespace biscuit::coins::sol;

namespace {
    const QString abandonAbout = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    QByteArray hex(const char *h) { return QByteArray::fromHex(h); }
    QJsonValue json(const char *text) { return QJsonDocument::fromJson(text).object().value("result"); }

    Account abandonAccount() {
        return std::move(*Account::fromSeed(*bip39::mnemonicToSeed(abandonAbout)));
    }

    Transfer sample(const Account &from) {
        Transfer t;
        t.from = from.publicKey();
        t.to = QByteArray(32, '\x35');
        t.lamports = 1500000000;
        t.recentBlockhash = QByteArray(32, '\x07');
        t.computeUnitLimit = transferComputeUnits;
        t.microLamportsPerUnit = 250000;
        return t;
    }
}

class TestSol : public QObject
{
Q_OBJECT

private slots:
    void base58() {
        QCOMPARE(toBase58(QByteArray(32, '\0')), QString("11111111111111111111111111111111"));
        QCOMPARE(toBase58(hex("0306466fe5211732ffecadba72c39be7bc8ce5bbc5f7126b2c439b3a40000000")),
                 QString("ComputeBudget111111111111111111111111111111"));
        QCOMPARE(fromBase58("ComputeBudget111111111111111111111111111111")->toHex(),
                 QByteArray("0306466fe5211732ffecadba72c39be7bc8ce5bbc5f7126b2c439b3a40000000"));
        QVERIFY(!fromBase58("0OIl"));   // not in the alphabet
        QVERIFY(!fromBase58(""));
    }

    void slip10() {
        // SLIP-0010, test vector 1 for Ed25519.
        const QByteArray seed = hex("000102030405060708090a0b0c0d0e0f");
        QByteArray chain;
        QCOMPARE(Account::slip10(seed, {}, &chain).toHex(), QByteArray("2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7"));
        QCOMPARE(chain.toHex(), QByteArray("90046a93de5380a72b5e45010748567d5ea02bbf6522f979e05c0d8d8ca9fffb"));
        QCOMPARE(Account::slip10(seed, {0}, &chain).toHex(), QByteArray("68e0fe46dfb67e368c75379acec591dad19df3cde26e63b93a8e704f1dade7a3"));
        QCOMPARE(chain.toHex(), QByteArray("8b59aa11380b624e81507a27fedda59fea6d0b779a778918a2fd3590e16e9c69"));
        QCOMPARE(Account::slip10(seed, {0, 1}, &chain).toHex(), QByteArray("b1d0bad404bf35da785a64ca1ac54b2617211d2777696fbffaf208f746ae84f2"));
        QCOMPARE(chain.toHex(), QByteArray("a320425f77d1b5c2505a6b1b27382b37368ee640e3557c315416801243552f14"));
    }

    void phantomAddress() {
        // m/44'/501'/0'/0': the address Phantom and Solflare show for this seed.
        const Account account = abandonAccount();
        QCOMPARE(addressText(account.publicKey()), QString("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk"));
    }

    void addresses() {
        QCOMPARE(parseAddress(" HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk ")->size(), 32);
        QString error;
        // No checksum in Solana addresses: a character less is often another
        // valid address (Send then says the address was never used); four
        // less are too short.
        QVERIFY(!parseAddress("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3D", &error));
        QCOMPARE(error, QString("Not a Solana address"));
        QVERIFY(!parseAddress("0x52908400098527886E0F7030069857D2E4169EE7"));
        QVERIFY(!parseAddress("1111111111111111111111111111111111"));   // 34 zero bytes
        QVERIFY(!parseAddress("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"));
    }

    void amounts() {
        QCOMPARE(*parseAmount("1.5"), quint64(1500000000));
        QCOMPARE(*parseAmount("0.000000001"), quint64(1));
        QVERIFY(!parseAmount("0.0000000001"));    // 10 decimals
        QVERIFY(!parseAmount("-1"));
        QVERIFY(!parseAmount("1e3"));
        QVERIFY(!parseAmount("18446744074"));     // over 64 bits of lamports
        QCOMPARE(formatAmount(1500000000), QString("1.5"));
        QCOMPARE(formatAmount(rentExemptMinimum), QString("0.00089088"));
    }

    void fee() {
        Transfer t;
        t.computeUnitLimit = 1000;
        t.microLamportsPerUnit = 250000;   // 250 lamports for the 1000 units
        QCOMPARE(t.fee(), quint64(5250));
        t.microLamportsPerUnit = 1;        // rounded up
        QCOMPARE(t.fee(), quint64(5001));
        t.microLamportsPerUnit = maximumPriorityFee;
        QCOMPARE(t.fee(), quint64(10000));
    }

    void signAndReadBack() {
        const Account account = abandonAccount();
        const Transfer t = sample(account);
        // The message, byte by byte: header, 4 accounts, blockhash, 3 instructions.
        const QByteArray m = t.message();
        QCOMPARE(m.left(4).toHex(), QByteArray("01000204"));
        QCOMPARE(m.size(), 4 + 4 * 32 + 32 + 1 + (3 + 5) + (3 + 9) + (5 + 12));
        QCOMPARE(m.right(17).toHex(), QByteArray("020200010c" "02000000" "002f685900000000"));

        QString error;
        const auto raw = sign(t, account, &error);
        QVERIFY2(raw, qPrintable(error));
        QCOMPARE(raw->size(), 1 + 64 + m.size());
        const auto back = decode(*raw, &error);
        QVERIFY2(back, qPrintable(error));
        QVERIFY(back->transfer == t);
        QCOMPARE(back->signature, raw->mid(1, 64));

        // One byte changed anywhere: refused.
        for (int i : {1, 70, int(raw->size()) - 1}) {
            QByteArray bad = *raw;
            bad[i] = char(bad.at(i) ^ 1);
            QVERIFY(!decode(bad));
        }
        QVERIFY(!decode(*raw + '\0'));
        QVERIFY(!decode(raw->left(raw->size() - 1)));
    }

    void refusedTransfers() {
        const Account account = abandonAccount();
        Transfer t = sample(account);
        t.to = t.from;
        QVERIFY(!sign(t, account));                  // to itself
        t = sample(account);
        t.from = QByteArray(32, '\x01');
        QVERIFY(!sign(t, account));                  // someone else's
        t = sample(account);
        t.recentBlockhash.chop(1);
        QVERIFY(!sign(t, account));
    }

    // ------------------------------------------------------------- replies

    void balanceAndBlockhash() {
        QCOMPARE(*parseBalance(json(R"({"jsonrpc":"2.0","result":{"context":{"apiVersion":"4.3.0","slot":458564016},"value":1141440},"id":2})")),
                 quint64(1141440));
        QVERIFY(!parseBalance(json(R"({"result":{"value":-1}})")));
        QVERIFY(!parseBalance(json(R"({"result":{"value":"5"}})")));
        const auto b = parseLatestBlockhash(json(R"({"result":{"context":{"slot":1},"value":{"blockhash":"ComputeBudget111111111111111111111111111111","lastValidBlockHeight":436750123}}})"));
        QVERIFY(b);
        QCOMPARE(b->lastValidBlockHeight, quint64(436750123));
        QCOMPARE(b->hash.size(), 32);
        QVERIFY(!parseLatestBlockhash(json(R"({"result":{"value":{"blockhash":"abc","lastValidBlockHeight":1}}})")));
    }

    void priorityFee() {
        QCOMPARE(*parsePriorityFee(json(R"({"result":[{"prioritizationFee":0,"slot":1},{"prioritizationFee":0,"slot":2}]})")),
                 minimumPriorityFee);
        QCOMPARE(*parsePriorityFee(json(R"({"result":[{"prioritizationFee":10,"slot":1},{"prioritizationFee":20000,"slot":2},
                                                       {"prioritizationFee":40000,"slot":3},{"prioritizationFee":90000,"slot":4}]})")),
                 quint64(90000));
        QCOMPARE(*parsePriorityFee(json(R"({"result":[{"prioritizationFee":900000000,"slot":1}]})")), maximumPriorityFee);
        QCOMPARE(*parsePriorityFee(json(R"({"result":[]})")), minimumPriorityFee);
        QVERIFY(!parsePriorityFee(json(R"({"result":[{"slot":1}]})")));
    }

    void signatures() {
        const auto list = parseSignatures(json(R"({"result":[{"blockTime":1792354515,"confirmationStatus":"finalized","err":null,"memo":null,
            "signature":"4rcYv2AEbgiR79vxNNVf99NocSCjXUXjFvMR7TSss2M5pEwDFHfov8oQSvBoYCPMnGR1QgrPMunmhM61j6rBuriv","slot":458563870},
            {"blockTime":null,"err":{"InstructionError":[0,"Custom"]},"signature":"NY8cFhz84RznWNUnLpGMgpeEfmiqUi6C3Zqo9jFVBL9swz2XdzHa4tXG1WUFqGpZWM49XHJPCEd4g2Eca2rezHh","slot":458563871}]})"));
        QVERIFY(list);
        QCOMPARE(list->size(), 2);
        QCOMPARE(list->at(0).time.toSecsSinceEpoch(), qint64(1792354515));
        QVERIFY(!list->at(0).failed);
        QVERIFY(list->at(1).failed);
        QVERIFY(!list->at(1).time.isValid());
        QVERIFY(!parseSignatures(json(R"({"result":[{"signature":"abc","slot":1}]})")));
    }

    void transactions() {
        // A SOL transfer of 0.25 from A to B, fee 5000 paid by A.
        const QByteArray a = *parseAddress("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk");
        const QByteArray b = QByteArray(32, '\x35');
        const QByteArray reply = QByteArray(R"({"result":{"blockTime":1792354515,"slot":458563870,
            "meta":{"err":null,"fee":5000,"preBalances":[1000000000,0,1],"postBalances":[749995000,250000000,1]},
            "transaction":{"signatures":["4rcYv2AEbgiR79vxNNVf99NocSCjXUXjFvMR7TSss2M5pEwDFHfov8oQSvBoYCPMnGR1QgrPMunmhM61j6rBuriv"],
            "message":{"accountKeys":[{"pubkey":"A","signer":true,"writable":true},{"pubkey":"B","signer":false,"writable":true},
                                      {"pubkey":"11111111111111111111111111111111","signer":false,"writable":false}],
            "instructions":[{"program":"system","programId":"11111111111111111111111111111111",
                             "parsed":{"type":"transfer","info":{"source":"A","destination":"B","lamports":250000000}}}]}}}})")
            .replace("\"A\"", ('"' + addressText(a) + '"').toLatin1()).replace("\"B\"", ('"' + addressText(b) + '"').toLatin1());
        const QJsonValue result = QJsonDocument::fromJson(reply).object().value("result");

        const auto sent = parseTransaction(result, a);
        QVERIFY(sent && sent->relevant);
        QVERIFY(!sent->entry.incoming);
        QCOMPARE(sent->entry.amount, quint64(250000000));
        QCOMPARE(sent->entry.fee, quint64(5000));
        QCOMPARE(sent->entry.counterparty, b);
        QCOMPARE(sent->entry.slot, quint64(458563870));

        const auto received = parseTransaction(result, b);
        QVERIFY(received && received->relevant);
        QVERIFY(received->entry.incoming);
        QCOMPARE(received->entry.amount, quint64(250000000));
        QCOMPARE(received->entry.fee, quint64(0));
        QCOMPARE(received->entry.counterparty, a);

        const auto other = parseTransaction(result, QByteArray(32, '\x09'));
        QVERIFY(other && !other->relevant);
        QVERIFY(!parseTransaction(QJsonValue(), a));
    }

    void statuses() {
        QVERIFY(!parseSignatureStatus(json(R"({"result":{"value":[]}})")));
        QVERIFY(!parseSignatureStatus(json(R"({"result":{"context":{"slot":1},"value":[null]}})"))->has_value());
        const auto s = parseSignatureStatus(json(R"({"result":{"value":[{"confirmationStatus":"confirmed","err":null,"slot":5}]}})"));
        QVERIFY(s && s->has_value() && (*s)->confirmed && !(*s)->failed);
        const auto f = parseSignatureStatus(json(R"({"result":{"value":[{"confirmationStatus":"processed","err":{"x":1}}]}})"));
        QVERIFY(f && f->has_value() && !(*f)->confirmed && (*f)->failed);
    }
};

QTEST_GUILESS_MAIN(TestSol)
#include "test_sol.moc"
