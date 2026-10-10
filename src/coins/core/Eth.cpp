// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Eth.h"

#include <wally_bip32.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "WallyInit.h"

extern "C" void biscuit_keccak(const uint8_t *in, size_t inlen, uint8_t *md, int mdlen);

namespace biscuit::coins::eth {

namespace {
    constexpr quint32 hardened(quint32 i) { return i | BIP32_INITIAL_HARDENED_CHILD; }
    constexpr u128 u128Max = ~u128(0);

    const unsigned char *bytes(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }

    void fail(QString *error, const QString &message) {
        if (error) *error = message;
    }

    // Big-endian bytes without leading zeros (0 -> empty).
    QByteArray minimalBytes(u128 value) {
        QByteArray out;
        while (value > 0) {
            out.prepend(char(quint8(value & 0xFF)));
            value >>= 8;
        }
        return out;
    }

    QByteArray word(const QByteArray &value) {   // left-padded to 32 bytes
        return QByteArray(32 - value.size(), '\0') + value;
    }

    // Integer from RLP bytes: canonical (no leading zero), at most 128 bits.
    std::optional<u128> rlpUint(const QByteArray &b) {
        if (b.size() > 16 || (!b.isEmpty() && b.at(0) == '\0')) {
            return std::nullopt;
        }
        u128 value = 0;
        for (char c : b) value = (value << 8) | quint8(c);
        return value;
    }

    std::optional<quint64> rlpUint64(const QByteArray &b) {
        const auto value = rlpUint(b);
        if (!value || *value > std::numeric_limits<quint64>::max()) {
            return std::nullopt;
        }
        return quint64(*value);
    }

    const QByteArray transferSelector = QByteArray::fromHex("a9059cbb");    // transfer(address,uint256)
    const QByteArray balanceOfSelector = QByteArray::fromHex("70a08231");   // balanceOf(address)
}

QByteArray keccak256(const QByteArray &data) {
    QByteArray md(32, '\0');
    biscuit_keccak(bytes(data), size_t(data.size()), reinterpret_cast<uint8_t *>(md.data()), 32);
    return md;
}

// ---------------------------------------------------------------- addresses

QString checksumAddress(const QByteArray &address) {
    const QByteArray lower = address.toHex();
    const QByteArray hash = keccak256(lower).toHex();
    QString out = "0x";
    for (int i = 0; i < lower.size(); ++i) {
        const char c = lower.at(i);
        // A letter is uppercase when its nibble of the hash is 8 or more.
        const bool upper = c >= 'a' && QByteArrayLiteral("0123456789abcdef").indexOf(hash.at(i)) >= 8;
        out += upper ? QChar(c).toUpper() : QChar(c);
    }
    return out;
}

std::optional<QByteArray> parseAddress(const QString &text, QString *error) {
    const QString t = text.trimmed();
    if (!t.startsWith("0x") && !t.startsWith("0X")) {
        fail(error, "An Ethereum address starts with 0x.");
        return std::nullopt;
    }
    const QString hex = t.mid(2);
    static const QString digits = "0123456789abcdefABCDEF";
    if (hex.size() != 40 || std::any_of(hex.begin(), hex.end(), [](QChar c) { return !digits.contains(c); })) {
        fail(error, "An Ethereum address has 40 hexadecimal characters after 0x.");
        return std::nullopt;
    }
    const QByteArray address = QByteArray::fromHex(hex.toLatin1());
    const bool mixed = hex != hex.toLower() && hex != hex.toUpper();
    if (mixed && checksumAddress(address).mid(2) != hex) {
        fail(error, "This address has a typo (its checksum does not match).");
        return std::nullopt;
    }
    return address;
}

QByteArray addressFromPublicKey(const QByteArray &publicKey) {
    ensureWallyInit();
    QByteArray full(EC_PUBLIC_KEY_UNCOMPRESSED_LEN, '\0');
    if (publicKey.size() == EC_PUBLIC_KEY_UNCOMPRESSED_LEN) {
        full = publicKey;
    } else if (publicKey.size() != EC_PUBLIC_KEY_LEN
               || wally_ec_public_key_decompress(bytes(publicKey), size_t(publicKey.size()),
                                                 reinterpret_cast<unsigned char *>(full.data()), size_t(full.size())) != WALLY_OK) {
        return {};
    }
    return keccak256(full.mid(1)).right(20);   // without the 0x04 prefix
}

QByteArray addressFromPrivateKey(const QByteArray &privateKey) {
    ensureWallyInit();
    QByteArray pub(EC_PUBLIC_KEY_LEN, '\0');
    if (privateKey.size() != EC_PRIVATE_KEY_LEN
        || wally_ec_public_key_from_private_key(bytes(privateKey), size_t(privateKey.size()),
                                                reinterpret_cast<unsigned char *>(pub.data()), size_t(pub.size())) != WALLY_OK) {
        return {};
    }
    return addressFromPublicKey(pub);
}

std::optional<Account> Account::fromSeed(const QByteArray &seed) {
    ensureWallyInit();
    ext_key master;
    if (bip32_key_from_seed(bytes(seed), size_t(seed.size()), BIP32_VER_MAIN_PRIVATE, 0, &master) != WALLY_OK) {
        return std::nullopt;
    }
    const uint32_t path[] = {hardened(44), hardened(60), hardened(0), 0};
    auto key = std::make_unique<ext_key>();
    const int ret = bip32_key_from_parent_path(&master, path, 4, BIP32_FLAG_KEY_PRIVATE, key.get());
    wally_bzero(&master, sizeof(master));
    if (ret != WALLY_OK) {
        wally_bzero(key.get(), sizeof(ext_key));
        return std::nullopt;
    }
    return Account(std::move(key));
}

Account::Account(std::unique_ptr<ext_key> key)
    : m_key(std::move(key))
{
}

Account::Account(Account &&) noexcept = default;
Account &Account::operator=(Account &&) noexcept = default;

Account::~Account() {
    if (m_key) {
        wally_bzero(m_key.get(), sizeof(ext_key));
    }
}

QByteArray Account::address(quint32 index) const {
    ext_key child;
    if (bip32_key_from_parent(m_key.get(), index, BIP32_FLAG_KEY_PUBLIC | BIP32_FLAG_SKIP_HASH, &child) != WALLY_OK) {
        return {};
    }
    const QByteArray pub(reinterpret_cast<const char *>(child.pub_key), sizeof(child.pub_key));
    wally_bzero(&child, sizeof(child));
    return addressFromPublicKey(pub);
}

QByteArray Account::privateKey(quint32 index) const {
    ext_key child;
    if (bip32_key_from_parent(m_key.get(), index, BIP32_FLAG_KEY_PRIVATE, &child) != WALLY_OK) {
        return {};
    }
    // priv_key[0] is a 0x00 prefix byte, the key is the 32 bytes after it.
    QByteArray priv(reinterpret_cast<const char *>(child.priv_key + 1), 32);
    wally_bzero(&child, sizeof(child));
    return priv;
}

// ------------------------------------------------------------------ amounts

std::optional<u128> parseAmount(const QString &text, int decimals) {
    const QString t = text.trimmed();
    const qsizetype dot = t.indexOf('.');
    const QString whole = dot < 0 ? t : t.left(dot);
    const QString fraction = dot < 0 ? QString() : t.mid(dot + 1);
    if ((whole.isEmpty() && fraction.isEmpty()) || fraction.size() > decimals || fraction.contains('.')) {
        return std::nullopt;
    }
    u128 value = 0;
    const QString digits = whole + fraction + QString(decimals - fraction.size(), '0');
    for (QChar c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const unsigned d = unsigned(c.unicode() - '0');
        if (value > (u128Max - d) / 10) {
            return std::nullopt;   // over 128 bits
        }
        value = value * 10 + d;
    }
    return value;
}

QString formatAmount(u128 value, int decimals) {
    QString digits;
    do {
        digits.prepend(QChar('0' + int(value % 10)));
        value /= 10;
    } while (value > 0);
    if (digits.size() <= decimals) {
        digits.prepend(QString(decimals - digits.size() + 1, '0'));
    }
    QString whole = digits.left(digits.size() - decimals);
    QString fraction = digits.right(decimals);
    while (fraction.endsWith('0')) fraction.chop(1);
    return fraction.isEmpty() ? whole : whole + "." + fraction;
}

std::optional<u128> parseQuantity(const QString &hex) {
    if (!hex.startsWith("0x") || hex.size() < 3 || hex.size() > 2 + 32) {
        return std::nullopt;
    }
    const QString digits = hex.mid(2);
    if (digits.size() > 1 && digits.startsWith('0')) {
        return std::nullopt;   // quantities have no leading zeros
    }
    u128 value = 0;
    for (QChar c : digits) {
        const int d = QStringLiteral("0123456789abcdef").indexOf(c.toLower());
        if (d < 0) {
            return std::nullopt;
        }
        value = (value << 4) | unsigned(d);
    }
    return value;
}

QString toQuantity(u128 value) {
    const QByteArray b = minimalBytes(value).toHex();
    QString s = QString::fromLatin1(b);
    while (s.startsWith('0')) s.remove(0, 1);
    return "0x" + (s.isEmpty() ? QString("0") : s);
}

std::optional<u128> parseUint256(const QByteArray &w) {
    if (w.size() != 32 || std::any_of(w.begin(), w.begin() + 16, [](char c) { return c != '\0'; })) {
        return std::nullopt;   // wrong size, or over 128 bits
    }
    u128 value = 0;
    for (int i = 16; i < 32; ++i) value = (value << 8) | quint8(w.at(i));
    return value;
}

// ---------------------------------------------------------------------- RLP

namespace rlp {

namespace {
    QByteArray header(int offset, qsizetype length) {
        if (length < 56) {
            return QByteArray(1, char(offset + length));
        }
        const QByteArray len = minimalBytes(u128(length));
        return QByteArray(1, char(offset + 55 + len.size())) + len;
    }
}

QByteArray bytes(const QByteArray &value) {
    if (value.size() == 1 && quint8(value.at(0)) < 0x80) {
        return value;
    }
    return header(0x80, value.size()) + value;
}

QByteArray uint(u128 value) {
    return bytes(minimalBytes(value));
}

QByteArray list(const QList<QByteArray> &encodedItems) {
    QByteArray payload;
    for (const QByteArray &item : encodedItems) payload += item;
    return header(0xC0, payload.size()) + payload;
}

namespace {
    // One item at `pos`: its kind, payload and total size. Canonical only.
    std::optional<QPair<Item, qsizetype>> readItem(const QByteArray &data, qsizetype pos) {
        if (pos >= data.size()) {
            return std::nullopt;
        }
        const quint8 b = quint8(data.at(pos));
        if (b < 0x80) {
            return qMakePair(Item{false, data.mid(pos, 1)}, qsizetype(1));
        }
        const bool isList = b >= 0xC0;
        const int base = isList ? 0xC0 : 0x80;
        qsizetype length = 0;
        qsizetype headerSize = 1;
        if (b - base < 56) {
            length = b - base;
        } else {
            const int lenSize = b - base - 55;
            if (lenSize > 4 || pos + 1 + lenSize > data.size() || data.at(pos + 1) == '\0') {
                return std::nullopt;
            }
            for (int i = 0; i < lenSize; ++i) length = (length << 8) | quint8(data.at(pos + 1 + i));
            if (length < 56) {
                return std::nullopt;   // should have used the short form
            }
            headerSize += lenSize;
        }
        if (pos + headerSize + length > data.size()) {
            return std::nullopt;
        }
        Item item{isList, data.mid(pos + headerSize, length)};
        if (!isList && length == 1 && quint8(item.payload.at(0)) < 0x80) {
            return std::nullopt;   // a single small byte encodes as itself
        }
        return qMakePair(item, headerSize + length);
    }
}

std::optional<Item> decode(const QByteArray &data) {
    const auto read = readItem(data, 0);
    if (!read || read->second != data.size()) {
        return std::nullopt;
    }
    return read->first;
}

std::optional<QList<Item>> decodeItems(const QByteArray &payload) {
    QList<Item> items;
    qsizetype pos = 0;
    while (pos < payload.size()) {
        const auto read = readItem(payload, pos);
        if (!read) {
            return std::nullopt;
        }
        items.append(read->first);
        pos += read->second;
    }
    return items;
}

}

// ------------------------------------------------------------- transactions

namespace {
    QList<QByteArray> fields(const Transaction &tx) {
        return {rlp::uint(tx.chainId), rlp::uint(tx.nonce), rlp::uint(tx.maxPriorityFeePerGas),
                rlp::uint(tx.maxFeePerGas), rlp::uint(tx.gasLimit), rlp::bytes(tx.to), rlp::uint(tx.value),
                rlp::bytes(tx.data), rlp::list({})};
    }
}

QByteArray Transaction::unsignedSerialized() const {
    return QByteArray(1, '\x02') + rlp::list(fields(*this));
}

QByteArray Transaction::signingHash() const {
    return keccak256(unsignedSerialized());
}

bool Transaction::operator==(const Transaction &o) const {
    return chainId == o.chainId && nonce == o.nonce && maxPriorityFeePerGas == o.maxPriorityFeePerGas
           && maxFeePerGas == o.maxFeePerGas && gasLimit == o.gasLimit && to == o.to && value == o.value
           && data == o.data;
}

std::optional<QByteArray> sign(const Transaction &tx, const QByteArray &privateKey, QString *error) {
    ensureWallyInit();
    if (tx.to.size() != 20) {
        fail(error, "Invalid recipient");
        return std::nullopt;
    }
    if (privateKey.size() != EC_PRIVATE_KEY_LEN) {
        fail(error, "Invalid key");
        return std::nullopt;
    }
    const QByteArray hash = tx.signingHash();
    // [header, r, s]: header = 27 + 4 (compressed) + recovery id. libsecp256k1
    // only produces low-s signatures, as Ethereum requires (EIP-2).
    QByteArray sig(EC_SIGNATURE_RECOVERABLE_LEN, '\0');
    if (wally_ec_sig_from_bytes(bytes(privateKey), size_t(privateKey.size()), bytes(hash), size_t(hash.size()),
                                EC_FLAG_ECDSA | EC_FLAG_RECOVERABLE,
                                reinterpret_cast<unsigned char *>(sig.data()), size_t(sig.size())) != WALLY_OK) {
        fail(error, "Unable to sign");
        return std::nullopt;
    }
    const quint8 yParity = (quint8(sig.at(0)) - 27) & 1;
    // r and s are integers: no leading zero bytes.
    auto integer = [](QByteArray b) {
        while (!b.isEmpty() && b.at(0) == '\0') b.remove(0, 1);
        return rlp::bytes(b);
    };
    QList<QByteArray> items = fields(tx);
    items << rlp::uint(yParity) << integer(sig.mid(1, 32)) << integer(sig.mid(33, 32));
    wally_bzero(sig.data(), size_t(sig.size()));
    return QByteArray(1, '\x02') + rlp::list(items);
}

std::optional<SignedTransaction> decode(const QByteArray &raw, QString *error) {
    ensureWallyInit();
    if (raw.isEmpty() || raw.at(0) != '\x02') {
        fail(error, "Not an EIP-1559 transaction");
        return std::nullopt;
    }
    const auto outer = rlp::decode(raw.mid(1));
    const auto items = outer && outer->isList ? rlp::decodeItems(outer->payload) : std::nullopt;
    if (!items || items->size() != 12) {
        fail(error, "Malformed transaction");
        return std::nullopt;
    }
    const QList<rlp::Item> &f = *items;
    for (int i = 0; i < 12; ++i) {
        if (f.at(i).isList != (i == 8)) {
            fail(error, "Malformed transaction");
            return std::nullopt;
        }
    }
    SignedTransaction out;
    const auto chainId = rlpUint64(f[0].payload), nonce = rlpUint64(f[1].payload), gas = rlpUint64(f[4].payload);
    const auto prio = rlpUint(f[2].payload), maxFee = rlpUint(f[3].payload), value = rlpUint(f[6].payload);
    const auto yParity = rlpUint(f[9].payload);
    if (!chainId || !nonce || !gas || !prio || !maxFee || !value || !yParity || *yParity > 1
        || f[5].payload.size() != 20 || !f[8].payload.isEmpty()
        || f[10].payload.size() > 32 || f[11].payload.size() > 32
        || f[10].payload.isEmpty() || f[11].payload.isEmpty()
        || f[10].payload.at(0) == '\0' || f[11].payload.at(0) == '\0') {
        fail(error, "Malformed transaction");
        return std::nullopt;
    }
    out.tx.chainId = *chainId;
    out.tx.nonce = *nonce;
    out.tx.maxPriorityFeePerGas = *prio;
    out.tx.maxFeePerGas = *maxFee;
    out.tx.gasLimit = *gas;
    out.tx.to = f[5].payload;
    out.tx.value = *value;
    out.tx.data = f[7].payload;

    // Sender: the public key recovered from the signature over the signing hash.
    const QByteArray hash = out.tx.signingHash();
    QByteArray sig(1, char(27 + 4 + int(*yParity)));
    sig += word(f[10].payload) + word(f[11].payload);
    QByteArray pub(EC_PUBLIC_KEY_LEN, '\0');
    if (wally_ec_sig_to_public_key(bytes(hash), size_t(hash.size()), bytes(sig), size_t(sig.size()),
                                   reinterpret_cast<unsigned char *>(pub.data()), size_t(pub.size())) != WALLY_OK) {
        fail(error, "Invalid signature");
        return std::nullopt;
    }
    out.from = addressFromPublicKey(pub);
    out.hash = keccak256(raw);
    return out;
}

// ------------------------------------------------------------------ ERC-20

const QList<Token> &tokens() {
    static const QList<Token> list{
        {"USDT", "Tether USD", QByteArray::fromHex("dac17f958d2ee523a2206206994597c13d831ec7"), 6},
        {"USDC", "USD Coin", QByteArray::fromHex("a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"), 6},
    };
    return list;
}

const Token *tokenByContract(const QByteArray &contract) {
    for (const Token &t : tokens()) {
        if (t.contract == contract) return &t;
    }
    return nullptr;
}

QByteArray erc20TransferData(const QByteArray &to, u128 amount) {
    return transferSelector + word(to) + word(minimalBytes(amount));
}

QByteArray erc20BalanceOfData(const QByteArray &owner) {
    return balanceOfSelector + word(owner);
}

std::optional<QPair<QByteArray, u128>> decodeErc20Transfer(const QByteArray &data) {
    if (data.size() != 4 + 32 + 32 || !data.startsWith(transferSelector)
        || std::any_of(data.begin() + 4, data.begin() + 16, [](char c) { return c != '\0'; })) {
        return std::nullopt;
    }
    const auto amount = parseUint256(data.mid(36, 32));
    if (!amount) {
        return std::nullopt;
    }
    return qMakePair(data.mid(16, 20), *amount);
}

}
