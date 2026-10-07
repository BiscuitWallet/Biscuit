// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include <QFile>

#include <openpgp/openpgp.h>

// The release signing key built into the app (src/assets/gpg_keys/biscuit.asc)
// against a message clearsigned with it, through the OpenPGP code the updater
// uses to check release hashes.
class TestReleaseSignature : public QObject
{
Q_OBJECT

private:
    static std::string read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string();
    }

    // As Updater::verifySignature: the signer's user id, or empty.
    static QString signer(const std::string &armoredMessage) {
        const openpgp::public_key_block key(read(BISCUIT_KEY_FILE));
        const openpgp::message_armored message(armoredMessage);
        const auto signature = openpgp::signature_rsa::from_armored(armoredMessage);
        const epee::span<const uint8_t> data = message;
        for (const auto &publicKey : key) {
            if (signature.verify(data, publicKey)) {
                return QString::fromStdString(key.user_id());
            }
        }
        return {};
    }

private slots:
    void signedByTheReleaseKey() {
        const std::string sample = read(BISCUIT_TEST_DATA_DIR "/release-signature-sample.asc");
        QVERIFY2(!sample.empty(), "tests/data/release-signature-sample.asc is missing");
        QCOMPARE(signer(sample), QString("Biscuit Wallet releases"));

        // One changed character in the signed hashes: refused.
        std::string tampered = sample;
        const auto pos = tampered.find("biscuit-1.0.0-test.zip");
        QVERIFY(pos != std::string::npos);
        tampered[pos] = 'B';
        bool refused = false;
        try {
            refused = signer(tampered).isEmpty();
        } catch (const std::exception &) {
            refused = true;
        }
        QVERIFY(refused);
    }

    // The same signature claiming SHA-512 instead of SHA-256: refused before
    // any verification, the updater only accepts SHA-256 signatures.
    void otherHashAlgorithmRefused() {
        const QString sample = QString::fromStdString(read(BISCUIT_TEST_DATA_DIR "/release-signature-sample.asc"));
        const int begin = sample.indexOf("-----BEGIN PGP SIGNATURE-----");
        QVERIFY(begin >= 0);
        QByteArray base64;
        bool body = false;
        for (const QString &line : sample.mid(begin).split('\n')) {
            const QString trimmed = line.trimmed();
            if (!body) {
                body = trimmed.isEmpty();  // armor headers end with a blank line
                continue;
            }
            if (trimmed.startsWith('=') || trimmed.startsWith("-----")) {
                break;                     // checksum or END line
            }
            base64 += trimmed.toLatin1();
        }
        QByteArray packet = QByteArray::fromBase64(base64);

        // Version 4, canonical text, RSA, then the hash algorithm (8 = SHA-256).
        const int at = packet.indexOf(QByteArray("\x04\x01\x01\x08", 4));
        QVERIFY(at >= 0);
        openpgp::signature_rsa::from_base64(packet.toBase64().toStdString());  // unchanged: parses
        packet[at + 3] = 10;  // SHA-512
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 openpgp::signature_rsa::from_base64(packet.toBase64().toStdString()));
    }
};

QTEST_GUILESS_MAIN(TestReleaseSignature)
#include "test_release_signature.moc"
