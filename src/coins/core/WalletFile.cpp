// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "WalletFile.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <cstring>
#include <utility>

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

namespace {
    QByteArray makeHeader(const KdfParams &kdf, const QByteArray &salt, const QByteArray &nonce) {
        QByteArray header;
        header.append(magic, magicLen);
        header.append(char(formatVersion));
        header.append(char(kdfArgon2id));
        appendU64(header, kdf.opsLimit);
        appendU64(header, kdf.memLimit);
        header.append(salt);
        header.append(nonce);
        return header;
    }

    unsigned char *secureKeyFrom(QByteArray &key) {
        auto *secure = static_cast<unsigned char *>(sodium_malloc(keyLen));
        if (secure) {
            std::memcpy(secure, key.constData(), keyLen);
        }
        wipe(key);
        return secure;
    }
}

std::optional<Session> Session::create(const QString &password, const KdfParams &kdf) {
    if (!sodiumReady()) {
        return std::nullopt;
    }
    Session session;
    session.m_kdf = kdf;
    session.m_salt = QByteArray(saltLen, Qt::Uninitialized);
    randombytes_buf(u(session.m_salt), session.m_salt.size());
    auto key = deriveKey(password, session.m_salt, kdf);
    if (!key || !(session.m_key = secureKeyFrom(*key))) {
        return std::nullopt;
    }
    return session;
}

std::optional<std::pair<Session, QByteArray>> Session::open(const QByteArray &file, const QString &password) {
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
    if (ret != 0) {
        wipe(*key);
        wipe(plain);
        return std::nullopt;
    }
    plain.resize(static_cast<qsizetype>(plainLen));

    Session session;
    session.m_kdf = kdf;
    session.m_salt = salt;
    if (!(session.m_key = secureKeyFrom(*key))) {
        wipe(plain);
        return std::nullopt;
    }
    return std::make_pair(std::move(session), plain);
}

Session::Session(Session &&other) noexcept
    : m_key(std::exchange(other.m_key, nullptr))
    , m_salt(std::move(other.m_salt))
    , m_kdf(other.m_kdf)
{
}

Session &Session::operator=(Session &&other) noexcept {
    if (this != &other) {
        if (m_key) sodium_free(m_key);
        m_key = std::exchange(other.m_key, nullptr);
        m_salt = std::move(other.m_salt);
        m_kdf = other.m_kdf;
    }
    return *this;
}

Session::~Session() {
    if (m_key) {
        sodium_free(m_key);   // wipes before freeing
    }
}

QByteArray Session::seal(const QByteArray &plaintext) const {
    if (!m_key) {
        return {};
    }
    QByteArray nonce(nonceLen, Qt::Uninitialized);
    randombytes_buf(u(nonce), nonce.size());
    const QByteArray header = makeHeader(m_kdf, m_salt, nonce);

    QByteArray cipher(plaintext.size() + tagLen, Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(u(cipher), &cipherLen, u(plaintext), plaintext.size(),
                                                   u(header), header.size(), nullptr, u(nonce), m_key) != 0) {
        return {};
    }
    cipher.resize(static_cast<qsizetype>(cipherLen));
    return header + cipher;
}

std::optional<QByteArray> Session::unseal(const QByteArray &file) const {
    if (!m_key || file.size() < headerLen + tagLen || !file.startsWith(QByteArray(magic, magicLen))) {
        return std::nullopt;
    }
    const qsizetype kdfOffset = magicLen + 2;
    if (file.mid(kdfOffset + 16, saltLen) != m_salt) {
        return std::nullopt;   // written with another key
    }
    const QByteArray header = file.left(headerLen);
    const QByteArray nonce = file.mid(kdfOffset + 16 + saltLen, nonceLen);
    const QByteArray cipher = file.mid(headerLen);
    QByteArray plain(cipher.size() - tagLen, Qt::Uninitialized);
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(u(plain), &plainLen, nullptr, u(cipher), cipher.size(),
                                                   u(header), header.size(), u(nonce), m_key) != 0) {
        wipe(plain);
        return std::nullopt;
    }
    plain.resize(static_cast<qsizetype>(plainLen));
    return plain;
}

namespace {
    bool writeFile(const QString &path, const QByteArray &data, QString *error) {
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
}

bool Session::save(const QString &path, const QByteArray &plaintext, QString *error) const {
    return writeFile(path, seal(plaintext), error);
}

QByteArray encrypt(const QByteArray &plaintext, const QString &password, const KdfParams &kdf) {
    const auto session = Session::create(password, kdf);
    return session ? session->seal(plaintext) : QByteArray();
}

std::optional<QByteArray> decrypt(const QByteArray &file, const QString &password) {
    auto opened = Session::open(file, password);
    if (!opened) {
        return std::nullopt;
    }
    return std::move(opened->second);
}

bool save(const QString &path, const QByteArray &plaintext, const QString &password, const KdfParams &kdf, QString *error) {
    return writeFile(path, encrypt(plaintext, password, kdf), error);
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
