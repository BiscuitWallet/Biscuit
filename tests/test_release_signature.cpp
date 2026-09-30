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
};

QTEST_GUILESS_MAIN(TestReleaseSignature)
#include "test_release_signature.moc"
