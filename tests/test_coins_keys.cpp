// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Keys and addresses of the BTC/LTC wallets, checked against the official test
// vectors of BIP39 and BIP84 and the Electrum protocol documentation.

#include <QSet>
#include <QtTest>

#include "Addresses.h"
#include "Bip39.h"
#include "CoinParams.h"
#include "HdAccount.h"

using namespace biscuit::coins;

namespace {
    const QString abandonAbout = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
}

class TestCoinsKeys : public QObject
{
Q_OBJECT

private slots:
    // --------------------------------------------------------------- BIP39

    void bip39OfficialVector() {
        // BIP39 test vector: entropy 00…00, passphrase "TREZOR".
        const auto seed = bip39::mnemonicToSeed(abandonAbout, "TREZOR");
        QVERIFY(seed.has_value());
        QCOMPARE(seed->toHex(), QByteArray("c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e53495531f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04"));
    }

    void bip39Validation() {
        QVERIFY(bip39::isValidMnemonic(abandonAbout));
        QVERIFY(bip39::isValidMnemonic("  ABANDON abandon abandon abandon abandon abandon\nabandon abandon abandon abandon abandon about "));
        // Bad checksum (last word), unknown word, wrong length.
        QVERIFY(!bip39::isValidMnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon"));
        QVERIFY(!bip39::isValidMnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon biscuit"));
        QVERIFY(!bip39::isValidMnemonic("abandon about"));
        QVERIFY(!bip39::isValidMnemonic(""));
        QVERIFY(!bip39::mnemonicToSeed("not a seed").has_value());
    }

    void bip39Generation() {
        QSet<QString> seen;
        for (int words : {12, 24}) {
            for (int i = 0; i < 20; ++i) {
                const QString m = bip39::generateMnemonic(words);
                QCOMPARE(m.split(' ').size(), words);
                QVERIFY(bip39::isValidMnemonic(m));
                QVERIFY2(!seen.contains(m), "generated seeds must never repeat");
                seen.insert(m);
            }
        }
        QVERIFY(bip39::generateMnemonic(13).isEmpty());
    }

    // --------------------------------------------------------------- BIP84

    void bip84BitcoinVectors() {
        // BIP84 test vectors, mnemonic "abandon … about", no passphrase.
        const auto seed = bip39::mnemonicToSeed(abandonAbout);
        QVERIFY(seed.has_value());
        const auto account = HdAccount::fromSeed(*seed, bitcoin());
        QVERIFY(account.has_value());

        QCOMPARE(account->publicKey(HdAccount::Receive, 0).toHex(),
                 QByteArray("0330d54fd0dd420a6e5f8d3624f5f3482cae350f79d5f0753bf5beef9c2d91af3c"));
        QCOMPARE(account->address(HdAccount::Receive, 0), QString("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"));
        QCOMPARE(account->address(HdAccount::Receive, 1), QString("bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g"));
        QCOMPARE(account->address(HdAccount::Change, 0), QString("bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el"));
        QCOMPARE(account->privateKey(HdAccount::Receive, 0).size(), 32);
    }

    void litecoinUsesItsOwnPath() {
        const auto seed = bip39::mnemonicToSeed(abandonAbout);
        const auto btc = HdAccount::fromSeed(*seed, bitcoin());
        const auto ltc = HdAccount::fromSeed(*seed, litecoin());
        QVERIFY(btc && ltc);
        const QString ltcAddress = ltc->address(HdAccount::Receive, 0);
        QVERIFY(ltcAddress.startsWith("ltc1q"));
        // Different coin type (m/84'/2'), so different keys than Bitcoin.
        QVERIFY(ltc->publicKey(HdAccount::Receive, 0) != btc->publicKey(HdAccount::Receive, 0));
        QVERIFY(isValidAddress(ltcAddress, litecoin()));
        QVERIFY(!isValidAddress(ltcAddress, bitcoin()));
    }

    void passphraseChangesKeys() {
        const auto a = HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout), bitcoin());
        const auto b = HdAccount::fromSeed(*bip39::mnemonicToSeed(abandonAbout, "extra"), bitcoin());
        QVERIFY(a->address(HdAccount::Receive, 0) != b->address(HdAccount::Receive, 0));
    }

    // ----------------------------------------------------------- addresses

    void bitcoinAddressValidation() {
        // Native SegWit v0 (BIP84), Taproot v1 (BIP86 vector), legacy P2PKH and P2SH.
        QVERIFY(isValidAddress("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", bitcoin()));
        QVERIFY(isValidAddress("BC1QCR8TE4KR609GCAWUTMRZA0J4XV80JY8Z306FYU", bitcoin()));
        QVERIFY(isValidAddress("bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr", bitcoin()));
        QVERIFY(isValidAddress("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", bitcoin()));
        QVERIFY(isValidAddress("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy", bitcoin()));

        // Typos and wrong networks.
        QVERIFY(!isValidAddress("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyv", bitcoin()));
        QVERIFY(!isValidAddress("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb", bitcoin()));
        QVERIFY(!isValidAddress("tb1qcr8te4kr609gcawutmrza0j4xv80jy8zeqchgx", bitcoin()));
        QVERIFY(!isValidAddress("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", litecoin()));
        QVERIFY(!isValidAddress("", bitcoin()));
        QVERIFY(!isValidAddress("888tNkZrPN6JsEgekjMnABU4TBzc2Dt29EPAvkRxbANsAnjyPbb3iQ1YBRk1UXcdRsiKc9dhwMVgN5S9cQUiyoogDavup3H", bitcoin()));
    }

    void scriptPubKeys() {
        QCOMPARE(addressToScriptPubKey("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", bitcoin())->toHex(),
                 QByteArray("0014c0cebcd6c3d3ca8c75dc5ec62ebe55330ef910e2"));
        QCOMPARE(addressToScriptPubKey("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", bitcoin())->toHex(),
                 QByteArray("76a91462e907b15cbf27d5425399ebf6f0fb50ebb88f1888ac"));
    }

    void electrumScriptHashExample() {
        // Example from the Electrum protocol documentation (genesis block address).
        const auto script = addressToScriptPubKey("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", bitcoin());
        QCOMPARE(electrumScriptHash(*script), QString("8b01df4e368ea28f8dc0423bcf7a4923e3a12d307c875e47a0cfbf90b5c39161"));
    }
};

QTEST_APPLESS_MAIN(TestCoinsKeys)
#include "test_coins_keys.moc"
