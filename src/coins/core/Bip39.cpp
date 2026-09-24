// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Bip39.h"

#include <QRandomGenerator>
#include <QRegularExpression>

#include <wally_bip39.h>
#include <wally_core.h>

#include "WallyInit.h"

namespace biscuit::coins::bip39 {

QString generateMnemonic(int wordCount) {
    // 12 words = 128 bits of entropy, 24 words = 256 bits.
    if (wordCount != 12 && wordCount != 24) {
        return {};
    }
    ensureWallyInit();

    const qsizetype entropyLen = wordCount == 12 ? 16 : 32;
    QByteArray entropy(entropyLen, Qt::Uninitialized);
    // QRandomGenerator::system() reads the OS CSPRNG (getentropy / arc4random / BCryptGenRandom).
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(entropy.data()), entropyLen / 4);

    char *words = nullptr;
    const int ret = bip39_mnemonic_from_bytes(nullptr, reinterpret_cast<const unsigned char *>(entropy.constData()),
                                              entropy.size(), &words);
    wally_bzero(entropy.data(), entropy.size());
    if (ret != WALLY_OK || !words) {
        return {};
    }
    QString mnemonic = QString::fromUtf8(words);
    wally_free_string(words);
    return mnemonic;
}

QString normalizeMnemonic(const QString &mnemonic) {
    static const QRegularExpression spaces("\\s+");
    return mnemonic.normalized(QString::NormalizationForm_KD).toLower().split(spaces, Qt::SkipEmptyParts).join(' ');
}

bool isValidMnemonic(const QString &mnemonic) {
    ensureWallyInit();
    const QByteArray words = normalizeMnemonic(mnemonic).toUtf8();
    return !words.isEmpty() && bip39_mnemonic_validate(nullptr, words.constData()) == WALLY_OK;
}

std::optional<QByteArray> mnemonicToSeed(const QString &mnemonic, const QString &passphrase) {
    if (!isValidMnemonic(mnemonic)) {
        return std::nullopt;
    }
    const QByteArray words = normalizeMnemonic(mnemonic).toUtf8();
    // BIP39: the passphrase is NFKD-normalized too, but case is kept.
    const QByteArray pass = passphrase.normalized(QString::NormalizationForm_KD).toUtf8();

    QByteArray seed(BIP39_SEED_LEN_512, Qt::Uninitialized);
    if (bip39_mnemonic_to_seed512(words.constData(), pass.isEmpty() ? nullptr : pass.constData(),
                                  reinterpret_cast<unsigned char *>(seed.data()), seed.size()) != WALLY_OK) {
        wally_bzero(seed.data(), seed.size());
        return std::nullopt;
    }
    return seed;
}

}
