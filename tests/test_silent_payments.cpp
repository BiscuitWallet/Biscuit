// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Addresses.h"
#include "Bip39.h"
#include "CoinParams.h"
#include "HdAccount.h"
#include "SilentPayments.h"
#include "Transactions.h"
#include <secp256k1.h>
#include <wally_core.h>
#include <wally_crypto.h>

using namespace biscuit::coins;

// The sending half of BIP-352's official test vectors
// (bitcoin/bips, bip-0352/send_and_receive_test_vectors.json).
class TestSilentPayments : public QObject
{
Q_OBJECT

private:
    static QByteArray hex(const QJsonValue &value) {
        return QByteArray::fromHex(value.toString().toLatin1());
    }

    static QByteArray compressedPublicKey(const QByteArray &privateKey) {
        QByteArray pub(EC_PUBLIC_KEY_LEN, '\0');
        wally_ec_public_key_from_private_key(reinterpret_cast<const unsigned char *>(privateKey.constData()), privateKey.size(),
                                             reinterpret_cast<unsigned char *>(pub.data()), pub.size());
        return pub;
    }

    static QByteArray hash160(const QByteArray &data) {
        QByteArray out(HASH160_LEN, '\0');
        wally_hash160(reinterpret_cast<const unsigned char *>(data.constData()), data.size(),
                      reinterpret_cast<unsigned char *>(out.data()), out.size());
        return out;
    }

    // Witness stack items from its serialization (varint count, varint-sized items).
    static QList<QByteArray> witnessItems(const QByteArray &w) {
        QList<QByteArray> items;
        qsizetype pos = 0;
        auto varint = [&]() -> quint64 {
            const quint8 first = static_cast<quint8>(w.at(pos++));
            if (first < 0xfd) {
                return first;
            }
            const int n = first == 0xfd ? 2 : first == 0xfe ? 4 : 8;
            quint64 value = 0;
            for (int i = 0; i < n; ++i) {
                value |= quint64(static_cast<quint8>(w.at(pos++))) << (8 * i);
            }
            return value;
        };
        if (w.isEmpty()) {
            return items;
        }
        const quint64 count = varint();
        for (quint64 i = 0; i < count; ++i) {
            const quint64 len = varint();
            items.append(w.mid(pos, len));
            pos += len;
        }
        return items;
    }

    // The input types BIP-352 counts: P2TR key path (not a script path with
    // the NUMS internal key), and P2WPKH, P2SH-P2WPKH, P2PKH spent with the
    // compressed key.
    static bool eligible(const QByteArray &script, const QByteArray &scriptSig, const QByteArray &witness,
                         const QByteArray &key, bool *taproot) {
        const QByteArray pkh = hash160(compressedPublicKey(key));
        if (script.size() == 34 && script.startsWith(QByteArray("\x51\x20", 2))) {
            QList<QByteArray> items = witnessItems(witness);
            if (items.size() > 1 && items.last().startsWith('\x50')) {
                items.removeLast();   // annex
            }
            if (items.size() > 1) {
                const QByteArray nums = QByteArray::fromHex("50929b74c1a04954b78b4b6035e97a5e078a5a0f28ec96d547bfee9ace803ac0");
                if (items.last().mid(1, 32) == nums) {
                    return false;
                }
            }
            *taproot = true;
            return true;
        }
        if (script.size() == 22 && script.startsWith(QByteArray("\x00\x14", 2))) {
            return script.mid(2) == pkh;
        }
        if (script.size() == 25 && script.startsWith(QByteArray("\x76\xa9\x14", 3))) {
            return script.mid(3, 20) == pkh;
        }
        if (script.size() == 23 && script.startsWith(QByteArray("\xa9\x14", 2))) {
            // P2SH-P2WPKH: scriptSig pushes the 22-byte redeem script 0014<pkh>.
            const QByteArray redeem = QByteArray("\x00\x14", 2) + pkh;
            return scriptSig == QByteArray(1, '\x16') + redeem && script.mid(2, 20) == hash160(redeem);
        }
        return false;
    }

    static QStringList sorted(QStringList list) {
        list.sort();
        return list;
    }

private slots:
    void sendingVectors_data() {
        QTest::addColumn<QJsonObject>("vector");
        QFile file(QStringLiteral(BISCUIT_TEST_DATA_DIR "/bip352_send_and_receive_test_vectors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
        QVERIFY(cases.size() >= 28);
        for (const QJsonValue &c : cases) {
            for (const QJsonValue &s : c.toObject().value("sending").toArray()) {
                QTest::newRow(c.toObject().value("comment").toString().toUtf8().constData()) << s.toObject();
            }
        }
    }

    void sendingVectors() {
        QFETCH(QJsonObject, vector);
        const QJsonObject given = vector.value("given").toObject();
        const QJsonObject expected = vector.value("expected").toObject();

        // Which inputs count (BIP-352 "inputs for shared secret derivation"),
        // from each input's script; in Biscuit the wallet only spends its own
        // P2WPKH coins, which always count.
        QList<sp::Input> inputs;
        for (const QJsonValue &v : given.value("vin").toArray()) {
            const QJsonObject vin = v.toObject();
            const QByteArray key = hex(vin.value("private_key"));
            const QByteArray script = hex(vin.value("prevout").toObject().value("scriptPubKey").toObject().value("hex"));
            bool taproot = false;
            const bool counts = eligible(script, hex(vin.value("scriptSig")), hex(vin.value("txinwitness")), key, &taproot);
            inputs.append({vin.value("txid").toString(), static_cast<quint32>(vin.value("vout").toInt()), key, taproot, counts});
        }
        const auto counting = std::count_if(inputs.begin(), inputs.end(), [](const sp::Input &in) { return in.counts; });
        QCOMPARE(int(counting), int(expected.value("input_pub_keys").toArray().size()));

        QList<sp::Address> recipients;
        for (const QJsonValue &r : given.value("recipients").toArray()) {
            const QJsonObject recipient = r.toObject();
            QString error;
            const auto address = sp::decodeAddress(recipient.value("address").toString(), &error);
            QVERIFY2(address.has_value(), qPrintable(error));
            QCOMPARE(address->scanKey, hex(recipient.value("scan_pub_key")));
            QCOMPARE(address->spendKey, hex(recipient.value("spend_pub_key")));
            const int count = recipient.contains("count") ? recipient.value("count").toInt() : 1;
            for (int i = 0; i < count; ++i) {
                recipients.append(*address);
            }
        }

        const QJsonArray outputSets = expected.value("outputs").toArray();
        const bool expectNone = outputSets.isEmpty() || outputSets.first().toArray().isEmpty();
        if (counting == 0) {
            QVERIFY(expectNone);   // no eligible input: nothing to send
            QVERIFY(!sp::outputKeys(inputs, recipients).has_value());
            return;
        }
        const auto keys = sp::outputKeys(inputs, recipients);
        if (expectNone) {
            QVERIFY(!keys.has_value());   // keys sum to zero, or K_max exceeded
            return;
        }
        QVERIFY(keys.has_value());
        QStringList got;
        for (const QByteArray &key : *keys) {
            got.append(QString::fromLatin1(key.toHex()));
        }
        bool matched = false;
        for (const QJsonValue &set : outputSets) {
            QStringList want;
            for (const QJsonValue &o : set.toArray()) {
                want.append(o.toString());
            }
            matched = matched || sorted(want) == sorted(got);
        }
        QVERIFY2(matched, qPrintable(got.join(", ")));
    }

    // A payment built from wallet coins, signed, and recognized by the
    // recipient from its own keys (the receiving side of BIP-352, written
    // here independently of the sending code).
    void walletPaymentIsFoundByRecipient() {
        const auto seed = bip39::mnemonicToSeed("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");
        const HdAccount account = std::move(*HdAccount::fromSeed(*seed, bitcoin()));
        QList<Utxo> coins;
        for (quint32 i = 0; i < 2; ++i) {
            Utxo u;
            u.txid = QString("%1").arg(i + 7).repeated(64).left(64);
            u.vout = i;
            u.value = 60000;
            u.index = i;
            u.height = 800000;
            coins.append(u);
        }
        const QByteArray bScan = QByteArray::fromHex("0f694e068028a717f8af6b9411f9a133dd3565258714cc226594b34db90c1f2c");
        const QByteArray bSpend = QByteArray::fromHex("9d6ad855ce3417ef84e836892e5a56392bfba05fa5d97ccea30e266f540e08b3");
        auto pub = [](const QByteArray &priv) {
            QByteArray out(EC_PUBLIC_KEY_LEN, '\0');
            wally_ec_public_key_from_private_key(reinterpret_cast<const unsigned char *>(priv.constData()), priv.size(),
                                                 reinterpret_cast<unsigned char *>(out.data()), out.size());
            return out;
        };
        const sp::Address recipient{pub(bScan), pub(bSpend), false};

        // As CoinWallet::planSend: plan with a placeholder, derive, replace.
        const QByteArray change = *addressToScriptPubKey("bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el", bitcoin());
        auto plan = planTransaction(coins, sp::taprootScript(QByteArray(32, '\0')), 100000, 3, change, false);
        QVERIFY(plan.has_value());
        QList<sp::Input> inputs;
        for (const Utxo &u : plan->inputs) {
            inputs.append({u.txid, u.vout, account.privateKey(u.chain, u.index), false, true});
        }
        const auto keys = sp::outputKeys(inputs, {recipient});
        QVERIFY(keys.has_value());
        for (int i = 0; i < plan->outputs.size(); ++i) {
            if (i != plan->changeOutput) {
                plan->outputs[i].scriptPubKey = sp::taprootScript(keys->first());
            }
        }
        const auto tx = signTransaction(*plan, account, 850000);
        QVERIFY(tx.has_value());
        QVERIFY(tx->hex.contains("5120" + keys->first().toHex()));
        QVERIFY(tx->vsize <= plan->estimatedVsize);

        // Recipient: A = sum of the inputs' public keys (as seen on chain),
        // shared = (input_hash · b_scan) · A, P = B_spend + t_0 · G.
        const secp256k1_context *ctx = secp256k1_context_static;
        QList<secp256k1_pubkey> points(plan->inputs.size());
        QList<const secp256k1_pubkey *> pointers;
        QByteArray smallest;
        for (int i = 0; i < plan->inputs.size(); ++i) {
            const Utxo &u = plan->inputs[i];
            const QByteArray p = pub(account.privateKey(u.chain, u.index));
            QVERIFY(secp256k1_ec_pubkey_parse(ctx, &points[i], reinterpret_cast<const unsigned char *>(p.constData()), p.size()));
            pointers.append(&points[i]);
            QByteArray outpoint = QByteArray::fromHex(u.txid.toLatin1());
            std::reverse(outpoint.begin(), outpoint.end());
            for (int b = 0; b < 4; ++b) outpoint.append(char((u.vout >> (8 * b)) & 0xff));
            if (smallest.isEmpty() || outpoint < smallest) smallest = outpoint;
        }
        secp256k1_pubkey A;
        QVERIFY(secp256k1_ec_pubkey_combine(ctx, &A, pointers.constData(), pointers.size()));
        QByteArray aBytes(33, '\0');
        size_t len = 33;
        secp256k1_ec_pubkey_serialize(ctx, reinterpret_cast<unsigned char *>(aBytes.data()), &len, &A, SECP256K1_EC_COMPRESSED);
        auto tagged = [](const char *tag, const QByteArray &m) {
            QByteArray out(32, '\0');
            wally_bip340_tagged_hash(reinterpret_cast<const unsigned char *>(m.constData()), m.size(), tag,
                                     reinterpret_cast<unsigned char *>(out.data()), out.size());
            return out;
        };
        const QByteArray inputHash = tagged("BIP0352/Inputs", smallest + aBytes);
        QByteArray scalar = bScan;
        QVERIFY(secp256k1_ec_seckey_tweak_mul(ctx, reinterpret_cast<unsigned char *>(scalar.data()),
                                              reinterpret_cast<const unsigned char *>(inputHash.constData())));
        QVERIFY(secp256k1_ec_pubkey_tweak_mul(ctx, &A, reinterpret_cast<const unsigned char *>(scalar.constData())));
        QByteArray shared(33, '\0');
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, reinterpret_cast<unsigned char *>(shared.data()), &len, &A, SECP256K1_EC_COMPRESSED);
        const QByteArray t0 = tagged("BIP0352/SharedSecret", shared + QByteArray(4, '\0'));
        QByteArray found(33, '\0');
        QCOMPARE(wally_ec_public_key_tweak(reinterpret_cast<const unsigned char *>(recipient.spendKey.constData()), 33,
                                           reinterpret_cast<const unsigned char *>(t0.constData()), 32,
                                           reinterpret_cast<unsigned char *>(found.data()), 33), WALLY_OK);
        QCOMPARE(found.mid(1), keys->first());

        // Litecoin has no silent payments; mainnet does not take tsp1….
        QVERIFY(!sp::addressFor("sp1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqwv", litecoin()));
        QVERIFY(sp::addressFor("sp1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqwv", bitcoin()));
        QVERIFY(isValidSendDestination("sp1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqwv", bitcoin()));
    }

    void addressChecks() {
        QVERIFY(sp::looksLikeAddress("sp1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqwv"));
        QVERIFY(!sp::looksLikeAddress("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"));
        // One changed character breaks the checksum.
        QVERIFY(!sp::decodeAddress("sp1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqww"));
        // Mixed case is not allowed.
        QVERIFY(!sp::decodeAddress("SP1qqgste7k9hx0qftg6qmwlkqtwuy6cycyavzmzj85c6qdfhjdpdjtdgqjuexzk6murw56suy3e0rd2cgqvycxttddwsvgxe2usfpxumr70xc9pkqwv"));
        QCOMPARE(sp::taprootScript(QByteArray(32, 'a')).size(), 34);
    }
};

QTEST_GUILESS_MAIN(TestSilentPayments)
#include "test_silent_payments.moc"
