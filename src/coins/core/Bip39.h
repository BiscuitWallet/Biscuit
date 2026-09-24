// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_BIP39_H
#define BISCUIT_BIP39_H

#include <optional>

#include <QByteArray>
#include <QString>

// BIP39 seed phrases shared by the BTC and LTC wallets. All cryptography is
// done by libwally-core; nothing here is home-made.
namespace biscuit::coins::bip39 {

    // 12 or 24 English words from the operating system's secure random source.
    // Returns an empty string for any other word count.
    QString generateMnemonic(int wordCount);

    // Checks the words and the checksum (whitespace and case are normalized).
    bool isValidMnemonic(const QString &mnemonic);

    // Lowercase, single spaces: the form used for the seed computation.
    QString normalizeMnemonic(const QString &mnemonic);

    // 64-byte seed (PBKDF2, as specified by BIP39). The optional passphrase is
    // the BIP39 "25th word". Returns std::nullopt for an invalid mnemonic.
    std::optional<QByteArray> mnemonicToSeed(const QString &mnemonic, const QString &passphrase = {});
}

#endif // BISCUIT_BIP39_H
