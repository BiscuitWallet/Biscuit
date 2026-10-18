// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Sol.h"

#include <limits>

#include <sodium.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "Eth.h"
#include "WallyInit.h"

namespace biscuit::coins::sol {

namespace {
    constexpr quint32 hardenedBit = 0x80000000u;

    const unsigned char *bytes(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }
    unsigned char *bytes(QByteArray &b) { return reinterpret_cast<unsigned char *>(b.data()); }

    void fail(QString *error, const QString &message) {
        if (error) *error = message;
    }

    bool ensureSodium() {
        static const bool ok = sodium_init() >= 0;
        return ok;
    }

    const QByteArray systemProgram(32, '\0');   // "11111111111111111111111111111111"
    const QByteArray &computeBudgetProgram() {
        static const QByteArray id = *fromBase58("ComputeBudget111111111111111111111111111111");
        return id;
    }

    QByteArray le(quint64 value, int size) {
        QByteArray out;
        for (int i = 0; i < size; ++i) out.append(char(quint8(value >> (8 * i))));
        return out;
    }

    // Solana's compact-u16: 7 bits per byte, low first.
    QByteArray compact(quint16 value) {
        QByteArray out;
        do {
            quint8 b = value & 0x7F;
            value >>= 7;
            if (value) b |= 0x80;
            out.append(char(b));
        } while (value);
        return out;
    }

    // Reads the message exactly as Transfer::message() writes it.
    class Reader {
    public:
        explicit Reader(const QByteArray &data) : m_data(data) {}
        bool take(int n, QByteArray *out) {
            if (n < 0 || m_pos + n > m_data.size()) return false;
            *out = m_data.mid(m_pos, n);
            m_pos += n;
            return true;
        }
        bool expect(const QByteArray &wanted) {
            QByteArray got;
            return take(int(wanted.size()), &got) && got == wanted;
        }
        bool uintLe(int size, quint64 *out) {
            QByteArray b;
            if (!take(size, &b)) return false;
            *out = 0;
            for (int i = size - 1; i >= 0; --i) *out = (*out << 8) | quint8(b.at(i));
            return true;
        }
        bool atEnd() const { return m_pos == m_data.size(); }
    private:
        const QByteArray &m_data;
        int m_pos = 0;
    };
}

QString toBase58(const QByteArray &data) {
    ensureWallyInit();
    char *out = nullptr;
    if (wally_base58_from_bytes(bytes(data), size_t(data.size()), 0, &out) != WALLY_OK || !out) {
        return {};
    }
    const QString text = QString::fromLatin1(out);
    wally_free_string(out);
    return text;
}

std::optional<QByteArray> fromBase58(const QString &text) {
    ensureWallyInit();
    const QByteArray latin = text.toLatin1();
    if (text.isEmpty() || latin.size() > 128 || QString::fromLatin1(latin) != text) {
        return std::nullopt;
    }
    QByteArray out(latin.size(), '\0');   // never longer than the text
    size_t written = 0;
    if (wally_base58_to_bytes(latin.constData(), 0, bytes(out), size_t(out.size()), &written) != WALLY_OK
        || written > size_t(out.size())) {
        return std::nullopt;
    }
    out.truncate(qsizetype(written));
    return out;
}

// ---------------------------------------------------------------- addresses

QString addressText(const QByteArray &publicKey) {
    return toBase58(publicKey);
}

std::optional<QByteArray> parseAddress(const QString &text, QString *error) {
    const auto key = fromBase58(text.trimmed());
    if (!key || key->size() != 32 || toBase58(*key) != text.trimmed()) {
        fail(error, "Not a Solana address");
        return std::nullopt;
    }
    return key;
}

QByteArray Account::slip10(const QByteArray &seed, const QList<quint32> &path, QByteArray *chainCode) {
    ensureWallyInit();
    QByteArray i(HMAC_SHA512_LEN, '\0');
    const QByteArray curve("ed25519 seed");
    if (wally_hmac_sha512(bytes(curve), size_t(curve.size()), bytes(seed), size_t(seed.size()), bytes(i), size_t(i.size())) != WALLY_OK) {
        return {};
    }
    // Ed25519 has hardened children only: 0x00 || key || index.
    for (quint32 index : path) {
        QByteArray data(1, '\0');
        data += i.left(32);
        data += char(quint8((index | hardenedBit) >> 24));
        data += char(quint8((index | hardenedBit) >> 16));
        data += char(quint8((index | hardenedBit) >> 8));
        data += char(quint8(index | hardenedBit));
        const QByteArray code = i.mid(32);
        const int ret = wally_hmac_sha512(bytes(code), size_t(code.size()), bytes(data), size_t(data.size()), bytes(i), size_t(i.size()));
        sodium_memzero(data.data(), size_t(data.size()));
        if (ret != WALLY_OK) {
            sodium_memzero(i.data(), size_t(i.size()));
            return {};
        }
    }
    if (chainCode) *chainCode = i.mid(32);
    QByteArray key = i.left(32);
    sodium_memzero(i.data(), size_t(i.size()));
    return key;
}

std::optional<Account> Account::fromSeed(const QByteArray &seed) {
    if (!ensureSodium()) {
        return std::nullopt;
    }
    QByteArray key = slip10(seed, {44, 501, 0, 0});
    if (key.size() != 32) {
        return std::nullopt;
    }
    Account account;
    account.m_public = QByteArray(crypto_sign_PUBLICKEYBYTES, '\0');
    account.m_secret = QByteArray(crypto_sign_SECRETKEYBYTES, '\0');
    const int ret = crypto_sign_seed_keypair(bytes(account.m_public), bytes(account.m_secret), bytes(key));
    sodium_memzero(key.data(), size_t(key.size()));
    if (ret != 0) {
        return std::nullopt;
    }
    return account;
}

Account::Account(Account &&) noexcept = default;
Account &Account::operator=(Account &&) noexcept = default;

Account::~Account() {
    if (!m_secret.isEmpty()) {
        sodium_memzero(m_secret.data(), size_t(m_secret.size()));
    }
}

QByteArray Account::sign(const QByteArray &message) const {
    if (m_secret.size() != crypto_sign_SECRETKEYBYTES) {
        return {};
    }
    QByteArray sig(crypto_sign_BYTES, '\0');
    if (crypto_sign_detached(bytes(sig), nullptr, bytes(message), size_t(message.size()), bytes(m_secret)) != 0) {
        return {};
    }
    return sig;
}

// ------------------------------------------------------------------ amounts

std::optional<quint64> parseAmount(const QString &text) {
    const auto value = eth::parseAmount(text, decimals);
    if (!value || *value > std::numeric_limits<quint64>::max()) {
        return std::nullopt;
    }
    return quint64(*value);
}

QString formatAmount(quint64 lamports) {
    return eth::formatAmount(lamports, decimals);
}

// ------------------------------------------------------------- transactions

QByteArray Transfer::message() const {
    QByteArray m;
    // Header: one signature (the sender); no read-only signer; two read-only
    // accounts (the programs).
    m += char(1);
    m += char(0);
    m += char(2);
    m += compact(4);
    m += from + to + systemProgram + computeBudgetProgram();
    m += recentBlockhash;
    m += compact(3);
    // SetComputeUnitLimit (2), SetComputeUnitPrice (3): Compute Budget, no accounts.
    m += char(3) + compact(0) + compact(5) + char(2) + le(computeUnitLimit, 4);
    m += char(3) + compact(0) + compact(9) + char(3) + le(microLamportsPerUnit, 8);
    // Transfer (2): System Program, from -> to.
    m += char(2) + compact(2) + char(0) + char(1) + compact(12) + le(2, 4) + le(lamports, 8);
    return m;
}

quint64 Transfer::fee() const {
    const unsigned __int128 micro = (unsigned __int128)computeUnitLimit * microLamportsPerUnit;
    return signatureFee + quint64((micro + 999999) / 1000000);
}

bool Transfer::operator==(const Transfer &other) const {
    return from == other.from && to == other.to && lamports == other.lamports && recentBlockhash == other.recentBlockhash
           && computeUnitLimit == other.computeUnitLimit && microLamportsPerUnit == other.microLamportsPerUnit;
}

std::optional<QByteArray> sign(const Transfer &transfer, const Account &account, QString *error) {
    if (transfer.from.size() != 32 || transfer.to.size() != 32 || transfer.recentBlockhash.size() != 32) {
        fail(error, "Invalid transfer");
        return std::nullopt;
    }
    if (transfer.from == transfer.to) {
        fail(error, "This is the address of this wallet");
        return std::nullopt;
    }
    if (transfer.from != account.publicKey()) {
        fail(error, "Not this wallet's address");
        return std::nullopt;
    }
    const QByteArray message = transfer.message();
    const QByteArray sig = account.sign(message);
    if (sig.size() != crypto_sign_BYTES) {
        fail(error, "Unable to sign");
        return std::nullopt;
    }
    return compact(1) + sig + message;
}

std::optional<SignedTransfer> decode(const QByteArray &raw, QString *error) {
    if (!ensureSodium()) {
        fail(error, "Unable to check the signature");
        return std::nullopt;
    }
    Reader r(raw);
    SignedTransfer out;
    Transfer &t = out.transfer;
    QByteArray programA, programB;
    quint64 limit = 0, price = 0, kind = 0;
    const bool ok = r.expect(compact(1)) && r.take(crypto_sign_BYTES, &out.signature)
        && r.expect(QByteArray("\x01\x00\x02", 3)) && r.expect(compact(4))
        && r.take(32, &t.from) && r.take(32, &t.to) && r.take(32, &programA) && r.take(32, &programB)
        && r.take(32, &t.recentBlockhash) && r.expect(compact(3))
        && r.expect(QByteArray("\x03\x00\x05\x02", 4)) && r.uintLe(4, &limit)
        && r.expect(QByteArray("\x03\x00\x09\x03", 4)) && r.uintLe(8, &price)
        && r.expect(QByteArray("\x02\x02\x00\x01\x0c", 5)) && r.uintLe(4, &kind) && r.uintLe(8, &t.lamports)
        && r.atEnd();
    if (!ok || kind != 2 || programA != systemProgram || programB != computeBudgetProgram() || t.from == t.to) {
        fail(error, "Not a SOL transfer from this wallet");
        return std::nullopt;
    }
    t.computeUnitLimit = quint32(limit);
    t.microLamportsPerUnit = price;
    const QByteArray message = t.message();
    if (message != raw.mid(1 + crypto_sign_BYTES)
        || crypto_sign_verify_detached(bytes(out.signature), bytes(message), size_t(message.size()), bytes(t.from)) != 0) {
        fail(error, "Invalid signature");
        return std::nullopt;
    }
    return out;
}

}
