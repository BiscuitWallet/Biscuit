// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_WALLETFILE_H
#define BISCUIT_WALLETFILE_H

#include <optional>

#include <QByteArray>
#include <QString>

// Encrypted container for the BTC/LTC wallet (seed, addresses in use, cache).
// Cryptography by libsodium only:
//   key   = Argon2id(password, random salt)       crypto_pwhash
//   data  = XChaCha20-Poly1305(key, random nonce)  crypto_aead_xchacha20poly1305_ietf
// The header (format version, KDF parameters, salt, nonce) is authenticated as
// associated data: any change to the file is detected.
//
// Layout (little-endian):
//   "BISCUITW" | u8 version | u8 kdf | u64 opslimit | u64 memlimit |
//   salt[16] | nonce[24] | ciphertext+tag
namespace biscuit::coins::walletfile {

    struct KdfParams {
        quint64 opsLimit;
        quint64 memLimit;   // bytes
    };

    // Argon2id "moderate" profile of libsodium (256 MiB, ~0.5-1 s): used for
    // real wallets. Stored in each file so it can be raised later.
    KdfParams defaultKdf();
    // Fast profile, for unit tests only.
    KdfParams testKdf();

    QByteArray encrypt(const QByteArray &plaintext, const QString &password, const KdfParams &kdf = defaultKdf());

    // std::nullopt for a wrong password or a damaged/modified file (the two
    // cannot be told apart, by design).
    std::optional<QByteArray> decrypt(const QByteArray &file, const QString &password);

    // Atomic write (temporary file + rename), readable by the owner only.
    bool save(const QString &path, const QByteArray &plaintext, const QString &password,
              const KdfParams &kdf = defaultKdf(), QString *error = nullptr);
    std::optional<QByteArray> load(const QString &path, const QString &password, QString *error = nullptr);

    // Overwrites a buffer holding secrets (compiler-proof).
    void wipe(QByteArray &data);
}

#endif // BISCUIT_WALLETFILE_H
