// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "WalletFile.h"

using namespace biscuit::coins;

namespace {
    const QByteArray secret = R"({"mnemonic":"abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"})";
    const QString password = "correct horse battery staple";

    // Header offsets (see WalletFile.h).
    constexpr int kdfOffset = 8 + 1 + 1;
    constexpr int headerLen = kdfOffset + 16 + 16 + 24;
}

class TestWalletFile : public QObject
{
Q_OBJECT

private slots:
    void roundTrip() {
        const QByteArray file = walletfile::encrypt(secret, password, walletfile::testKdf());
        QVERIFY(file.startsWith("BISCUITW"));
        QVERIFY(!file.contains("abandon"));   // nothing readable in the file
        QCOMPARE(walletfile::decrypt(file, password), std::optional<QByteArray>(secret));
    }

    void wrongPassword() {
        const QByteArray file = walletfile::encrypt(secret, password, walletfile::testKdf());
        QVERIFY(!walletfile::decrypt(file, "correct horse battery stapl").has_value());
        QVERIFY(!walletfile::decrypt(file, "").has_value());
    }

    void unicodePassword() {
        const QString pw = QString::fromUtf8("mot de passe éèà 🍪");
        const QByteArray file = walletfile::encrypt(secret, pw, walletfile::testKdf());
        QCOMPARE(walletfile::decrypt(file, pw), std::optional<QByteArray>(secret));
    }

    void freshSaltAndNonceEachTime() {
        const QByteArray a = walletfile::encrypt(secret, password, walletfile::testKdf());
        const QByteArray b = walletfile::encrypt(secret, password, walletfile::testKdf());
        QVERIFY(a != b);
        QVERIFY(a.mid(kdfOffset + 16, 16 + 24) != b.mid(kdfOffset + 16, 16 + 24));
    }

    void anyModificationIsDetected() {
        const QByteArray file = walletfile::encrypt(secret, password, walletfile::testKdf());
        // Flip one bit in every byte position: header (version, KDF params,
        // salt, nonce), ciphertext and tag.
        for (int i = 8; i < file.size(); ++i) {
            QByteArray tampered = file;
            tampered[i] = char(tampered.at(i) ^ 0x01);
            QVERIFY2(!walletfile::decrypt(tampered, password).has_value(), qPrintable(QString("byte %1").arg(i)));
        }
        QVERIFY(!walletfile::decrypt(file.left(file.size() - 1), password).has_value());
        QVERIFY(!walletfile::decrypt(file.left(headerLen), password).has_value());
        QVERIFY(!walletfile::decrypt(file + "x", password).has_value());
    }

    void rejectsForeignOrCraftedFiles() {
        QVERIFY(!walletfile::decrypt("", password).has_value());
        QVERIFY(!walletfile::decrypt("not a wallet at all, just some text that is long enough to parse.....", password).has_value());

        // A crafted header asking for 1 TiB of memory must be refused before
        // running the KDF (no memory exhaustion).
        QByteArray crafted = walletfile::encrypt(secret, password, walletfile::testKdf());
        const quint64 huge = qToLittleEndian<quint64>(1ULL << 40);
        crafted.replace(kdfOffset + 8, 8, QByteArray(reinterpret_cast<const char *>(&huge), 8));
        QElapsedTimer t;
        t.start();
        QVERIFY(!walletfile::decrypt(crafted, password).has_value());
        QVERIFY(t.elapsed() < 1000);
    }

    void defaultKdfIsStrong() {
        const auto kdf = walletfile::defaultKdf();
        QVERIFY(kdf.memLimit >= 256ULL * 1024 * 1024);
        QVERIFY(kdf.opsLimit >= 3);
    }

    void saveAndLoadFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test.btcltc");

        QString error;
        QVERIFY2(walletfile::save(path, secret, password, walletfile::testKdf(), &error), qPrintable(error));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFileDevice::Permissions());
        QCOMPARE(walletfile::load(path, password), std::optional<QByteArray>(secret));
        QVERIFY(!walletfile::load(path, "wrong", &error).has_value());
        QCOMPARE(error, QString("Wrong password or damaged file"));
        QVERIFY(!walletfile::load(dir.filePath("missing"), password).has_value());
    }

    void wipeClearsBuffer() {
        QByteArray data = "secret words";
        walletfile::wipe(data);
        QVERIFY(data.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestWalletFile)
#include "test_wallet_file.moc"
