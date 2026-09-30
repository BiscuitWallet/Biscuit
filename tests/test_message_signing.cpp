// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include "Addresses.h"
#include "CoinParams.h"
#include "MessageSigning.h"
#include <wally_address.h>
#include <wally_core.h>
#include <wally_crypto.h>

using namespace biscuit::coins;

class TestMessageSigning : public QObject
{
Q_OBJECT

private:
    static QByteArray fromWif(const char *wif) {
        QByteArray key(EC_PRIVATE_KEY_LEN, '\0');
        wally_wif_to_bytes(wif, WALLY_ADDRESS_VERSION_WIF_MAINNET, WALLY_WIF_FLAG_COMPRESSED,
                           reinterpret_cast<unsigned char *>(key.data()), key.size());
        return key;
    }

private slots:
    // Reference vector of bitcoinjs-message (README): deterministic signature.
    void referenceVector() {
        const QByteArray key = fromWif("L4rK1yDtCWekvXuE6oXD9jCYfFNV2cWRpVuPLBcCU2z8TrisoyY1");
        const QString message = "This is an example of a signed message.";
        const QString expected = "H9L5yLFjti0QTHhPyFrZCT1V/MMnBtXKmoiKDZ78NDBjERki6ZTQZdSMCtkgoNmp17By9ItJr8o7ChX0XxY91nk=";
        QCOMPARE(message::sign(key, message, bitcoin()), expected);
        QVERIFY(message::verify("1F3sAm6ZtwLAUnj7d38pGFxtP3RVEvtsbV", message, expected, bitcoin()));
        QVERIFY(!message::verify("1F3sAm6ZtwLAUnj7d38pGFxtP3RVEvtsbV", message + " ", expected, bitcoin()));
        QVERIFY(!message::verify("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", message, expected, bitcoin()));
    }

    void segwitAddresses() {
        const QByteArray key = fromWif("L4rK1yDtCWekvXuE6oXD9jCYfFNV2cWRpVuPLBcCU2z8TrisoyY1");
        QByteArray pub(EC_PUBLIC_KEY_LEN, '\0');
        wally_ec_public_key_from_private_key(reinterpret_cast<const unsigned char *>(key.constData()), key.size(),
                                             reinterpret_cast<unsigned char *>(pub.data()), pub.size());
        for (const CoinParams *params : {&bitcoin(), &litecoin()}) {
            const QString address = p2wpkhAddress(pub, *params);
            const QString sig = message::sign(key, "Biscuit", *params);
            QVERIFY(message::verify(address, "Biscuit", sig, *params));
            // BIP-137 header for a native SegWit address (39-42) verifies too.
            QByteArray raw = QByteArray::fromBase64(sig.toLatin1());
            raw[0] = char(quint8(raw[0]) - 31 + 39);
            QVERIFY(message::verify(address, "Biscuit", QString::fromLatin1(raw.toBase64()), *params));
            // A Bitcoin signature is not a Litecoin one (different prefix).
            const CoinParams &other = *params == bitcoin() ? litecoin() : bitcoin();
            QVERIFY(!message::verify(p2wpkhAddress(pub, other), "Biscuit", sig, other));
        }
        QVERIFY(!message::verify("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", "x", "not base64!", bitcoin()));
    }
};

QTEST_GUILESS_MAIN(TestMessageSigning)
#include "test_message_signing.moc"
