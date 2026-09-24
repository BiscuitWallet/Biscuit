// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "WalletFile.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <sodium.h>

namespace biscuit::coins::walletfile {

namespace {
    constexpr char magic[] = "BISCUITW";
    constexpr qsizetype magicLen = 8;
    constexpr quint8 formatVersion = 1;
    constexpr quint8 kdfArgon2id = 1;

    constexpr qsizetype saltLen = crypto_pwhash_SALTBYTES;
    constexpr qsizetype nonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    constexpr qsizetype keyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
    constexpr qsizetype tagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;
    constexpr qsizetype headerLen = magicLen + 1 + 1 + 8 + 8 + saltLen + nonceLen;

    bool sodiumReady() {
        static const bool ok = sodium_init() >= 0;
        return ok;
    }

    unsigned char *u(QByteArray &b) { return reinterpret_cast<unsigned char *>(b.data()); }
    const unsigned char *u(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }

    void appendU64(QByteArray &out, quint64 v) {
        const quint64 le = qToLittleEndian(v);
        out.append(reinterpret_cast<const char *>(&le), sizeof(le));
    }

    std::optional<QByteArray> deriveKey(const QString &password, const QByteArray &salt, const KdfParams &kdf) {
        QByteArray pass = password.toUtf8();
        QByteArray key(keyLen, Qt::Uninitialized);
        const int ret = crypto_pwhash(u(key), key.size(), pass.constData(), pass.size(), u(salt),
                                      kdf.opsLimit, kdf.memLimit, crypto_pwhash_ALG_ARGON2ID13);
        wipe(pass);
        if (ret != 0) {
            wipe(key);
            return std::nullopt;
        }
        return key;
    }
}

KdfParams defaultKdf() {
    return {crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE};
}

KdfParams testKdf() {
    return {crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN};
}

void wipe(QByteArray &data) {
    if (!data.isEmpty()) {
        sodium_memzero(data.data(), data.size());
    }
    data.clear();
}

QByteArray encrypt(const QByteArray &plaintext, const QString &password, const KdfParams &kdf) {
    if (!sodiumReady()) {
        return {};
    }

    QByteArray salt(saltLen, Qt::Uninitialized);
    QByteArray nonce(nonceLen, Qt::Uninitialized);
    randombytes_buf(u(salt), salt.size());
    randombytes_buf(u(nonce), nonce.size());

    QByteArray header;
    header.append(magic, magicLen);
    header.append(char(formatVersion));
    header.append(char(kdfArgon2id));
    appendU64(header, kdf.opsLimit);
    appendU64(header, kdf.memLimit);
    header.append(salt);
    header.append(nonce);

    auto key = deriveKey(password, salt, kdf);
    if (!key) {
        return {};
    }

    QByteArray cipher(plaintext.size() + tagLen, Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    const int ret = crypto_aead_xchacha20poly1305_ietf_encrypt(
            u(cipher), &cipherLen, u(plaintext), plaintext.size(),
            u(header), header.size(), nullptr, u(nonce), u(*key));
    wipe(*key);
    if (ret != 0) {
        return {};
    }
    cipher.resize(static_cast<qsizetype>(cipherLen));
    return header + cipher;
}

std::optional<QByteArray> decrypt(const QByteArray &file, const QString &password) {
    if (!sodiumReady() || file.size() < headerLen + tagLen || !file.startsWith(QByteArray(magic, magicLen))) {
        return std::nullopt;
    }
    if (quint8(file.at(magicLen)) != formatVersion || quint8(file.at(magicLen + 1)) != kdfArgon2id) {
        return std::nullopt;
    }

    const qsizetype kdfOffset = magicLen + 2;
    const KdfParams kdf{qFromLittleEndian<quint64>(file.constData() + kdfOffset),
                        qFromLittleEndian<quint64>(file.constData() + kdfOffset + 8)};
    // Refuse absurd parameters from a crafted file (memory exhaustion).
    if (kdf.opsLimit < crypto_pwhash_OPSLIMIT_MIN || kdf.opsLimit > crypto_pwhash_OPSLIMIT_SENSITIVE
        || kdf.memLimit < crypto_pwhash_MEMLIMIT_MIN || kdf.memLimit > crypto_pwhash_MEMLIMIT_SENSITIVE) {
        return std::nullopt;
    }

    const QByteArray header = file.left(headerLen);
    const QByteArray salt = file.mid(kdfOffset + 16, saltLen);
    const QByteArray nonce = file.mid(kdfOffset + 16 + saltLen, nonceLen);
    const QByteArray cipher = file.mid(headerLen);

    auto key = deriveKey(password, salt, kdf);
    if (!key) {
        return std::nullopt;
    }

    QByteArray plain(cipher.size() - tagLen, Qt::Uninitialized);
    unsigned long long plainLen = 0;
    const int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
            u(plain), &plainLen, nullptr, u(cipher), cipher.size(),
            u(header), header.size(), u(nonce), u(*key));
    wipe(*key);
    if (ret != 0) {
        wipe(plain);
        return std::nullopt;
    }
    plain.resize(static_cast<qsizetype>(plainLen));
    return plain;
}

bool save(const QString &path, const QByteArray &plaintext, const QString &password, const KdfParams &kdf, QString *error) {
    const QByteArray data = encrypt(plaintext, password, kdf);
    if (data.isEmpty()) {
        if (error) *error = "Encryption failed";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

std::optional<QByteArray> load(const QString &path, const QString &password, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    auto plain = decrypt(file.readAll(), password);
    if (!plain && error) {
        *error = "Wrong password or damaged file";
    }
    return plain;
}

}
