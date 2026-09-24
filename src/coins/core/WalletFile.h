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

    // An unlocked wallet file: the key is derived once (Argon2id is slow on
    // purpose) and kept in memory protected by libsodium (locked in RAM, guard
    // pages, wiped on destruction). Each save uses a fresh random nonce.
    class Session {
    public:
        // New file: random salt.
        static std::optional<Session> create(const QString &password, const KdfParams &kdf = defaultKdf());
        // Existing file: returns the session and the decrypted content.
        static std::optional<std::pair<Session, QByteArray>> open(const QByteArray &file, const QString &password);

        Session(Session &&other) noexcept;
        Session &operator=(Session &&other) noexcept;
        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;
        ~Session();

        QByteArray seal(const QByteArray &plaintext) const;
        // Decrypts a file written with this session's key (same salt).
        std::optional<QByteArray> unseal(const QByteArray &file) const;
        bool save(const QString &path, const QByteArray &plaintext, QString *error = nullptr) const;

    private:
        Session() = default;
        unsigned char *m_key = nullptr;   // sodium_malloc'd
        QByteArray m_salt;
        KdfParams m_kdf{};
    };
}

#endif // BISCUIT_WALLETFILE_H
