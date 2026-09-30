// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SilentPayments.h"

#include <algorithm>
#include <cstring>

#include <secp256k1.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "WallyInit.h"

namespace biscuit::coins::sp {

namespace {
    void fail(QString *error, const QString &message) {
        if (error) {
            *error = message;
        }
    }

    const unsigned char *bytes(const QByteArray &data) {
        return reinterpret_cast<const unsigned char *>(data.constData());
    }

    unsigned char *bytes(QByteArray &data) {
        return reinterpret_cast<unsigned char *>(data.data());
    }

    // ---- bech32m (BIP-350), without the 90-character limit of addresses ----

    constexpr char charset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    constexpr quint32 bech32mConst = 0x2bc830a3;

    quint32 polymod(const QList<quint8> &values) {
        static constexpr quint32 gen[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
        quint32 chk = 1;
        for (quint8 v : values) {
            const quint8 top = chk >> 25;
            chk = ((chk & 0x1ffffff) << 5) ^ v;
            for (int i = 0; i < 5; ++i) {
                if ((top >> i) & 1) {
                    chk ^= gen[i];
                }
            }
        }
        return chk;
    }

    // Returns the data part (5-bit values, checksum removed) for a valid
    // bech32m string with the expected human-readable part.
    std::optional<QList<quint8>> bech32mDecode(const QString &text, QString *hrpOut) {
        if (text.size() < 8 || text.size() > 1023) {
            return std::nullopt;
        }
        const bool hasLower = text != text.toUpper();
        const bool hasUpper = text != text.toLower();
        if (hasLower && hasUpper) {
            return std::nullopt;   // mixed case
        }
        const QString s = text.toLower();
        const qsizetype sep = s.lastIndexOf('1');
        if (sep < 1 || sep + 7 > s.size()) {
            return std::nullopt;
        }
        const QString hrp = s.left(sep);
        QList<quint8> values;
        for (QChar c : hrp) {
            if (c.unicode() < 33 || c.unicode() > 126) {
                return std::nullopt;
            }
            values.append(c.unicode() >> 5);
        }
        values.append(0);
        for (QChar c : hrp) {
            values.append(c.unicode() & 31);
        }
        QList<quint8> data;
        for (qsizetype i = sep + 1; i < s.size(); ++i) {
            const char *pos = std::strchr(charset, s.at(i).toLatin1());
            if (!pos || s.at(i).toLatin1() == '\0') {
                return std::nullopt;
            }
            data.append(static_cast<quint8>(pos - charset));
        }
        if (polymod(values + data) != bech32mConst) {
            return std::nullopt;
        }
        data.resize(data.size() - 6);
        *hrpOut = hrp;
        return data;
    }

    // 5-bit groups to bytes; leftover bits must be fewer than 5 and zero.
    std::optional<QByteArray> fromFiveBits(const QList<quint8> &values) {
        QByteArray out;
        quint32 acc = 0;
        int bits = 0;
        for (quint8 v : values) {
            acc = (acc << 5) | v;
            bits += 5;
            if (bits >= 8) {
                bits -= 8;
                out.append(static_cast<char>((acc >> bits) & 0xff));
            }
        }
        if (bits >= 5 || ((acc << (8 - bits)) & 0xff)) {
            return std::nullopt;
        }
        return out;
    }

    // ---- BIP-352 ----

    std::optional<QByteArray> taggedHash(const char *tag, const QByteArray &message) {
        QByteArray out(SHA256_LEN, '\0');
        if (wally_bip340_tagged_hash(bytes(message), message.size(), tag, bytes(out), out.size()) != WALLY_OK) {
            return std::nullopt;
        }
        return out;
    }

    bool validScalar(const QByteArray &scalar) {
        return scalar.size() == 32 && secp256k1_ec_seckey_verify(secp256k1_context_static, bytes(scalar)) == 1;
    }

    std::optional<QByteArray> publicKey(const QByteArray &privateKey) {
        QByteArray out(EC_PUBLIC_KEY_LEN, '\0');
        if (wally_ec_public_key_from_private_key(bytes(privateKey), privateKey.size(), bytes(out), out.size()) != WALLY_OK) {
            return std::nullopt;
        }
        return out;
    }

    // Outpoint as serialized in a transaction: txid (little-endian) || vout (LE).
    QByteArray serializeOutpoint(const Input &input) {
        QByteArray txid = QByteArray::fromHex(input.txid.toLatin1());
        std::reverse(txid.begin(), txid.end());
        QByteArray out = txid;
        for (int i = 0; i < 4; ++i) {
            out.append(static_cast<char>((input.vout >> (8 * i)) & 0xff));
        }
        return out;
    }

    QByteArray bigEndian32(quint32 value) {
        QByteArray out(4, '\0');
        for (int i = 0; i < 4; ++i) {
            out[3 - i] = static_cast<char>((value >> (8 * i)) & 0xff);
        }
        return out;
    }
}

bool looksLikeAddress(const QString &text) {
    const QString s = text.trimmed().toLower();
    return s.startsWith("sp1") || s.startsWith("tsp1");
}

std::optional<Address> decodeAddress(const QString &address, QString *error) {
    ensureWallyInit();
    QString hrp;
    const auto data = bech32mDecode(address.trimmed(), &hrp);
    if (!data || data->isEmpty()) {
        fail(error, "This is not a valid silent payment address.");
        return std::nullopt;
    }
    if (hrp != "sp" && hrp != "tsp") {
        fail(error, "This is not a Bitcoin silent payment address.");
        return std::nullopt;
    }
    const quint8 version = data->first();
    if (version == 31) {
        fail(error, "This silent payment address version is not supported.");
        return std::nullopt;
    }
    auto payload = fromFiveBits(data->mid(1));
    if (!payload || payload->size() < 66 || (version == 0 && payload->size() != 66)) {
        fail(error, "This is not a valid silent payment address.");
        return std::nullopt;
    }
    Address result{payload->left(33), payload->mid(33, 33), hrp == "tsp"};
    for (const QByteArray &key : {result.scanKey, result.spendKey}) {
        if (wally_ec_public_key_verify(bytes(key), key.size()) != WALLY_OK) {
            fail(error, "This silent payment address holds an invalid key.");
            return std::nullopt;
        }
    }
    return result;
}

std::optional<Address> addressFor(const QString &address, const CoinParams &params, QString *error) {
    if (!looksLikeAddress(address) || (params.bech32Hrp != "bc" && params.bech32Hrp != "tb")) {
        fail(error, "Silent payments are for Bitcoin only.");
        return std::nullopt;
    }
    auto decoded = decodeAddress(address, error);
    if (decoded && decoded->testnet != (params.bech32Hrp == "tb")) {
        fail(error, "This silent payment address is for another network.");
        return std::nullopt;
    }
    return decoded;
}

std::optional<QList<QByteArray>> outputKeys(const QList<Input> &inputs, const QList<Address> &recipients, QString *error) {
    ensureWallyInit();
    const secp256k1_context *ctx = secp256k1_context_static;
    if (inputs.isEmpty()) {
        fail(error, "A silent payment needs at least one input.");
        return std::nullopt;
    }

    // a = sum of the input private keys (negated for Taproot keys with odd y).
    // An intermediate sum may be zero; only the final one may not.
    std::optional<QByteArray> sum;
    bool anyCounts = false;
    for (const Input &input : inputs) {
        if (!input.counts) {
            continue;
        }
        anyCounts = true;
        QByteArray key = input.privateKey;
        const auto pub = publicKey(key);
        if (!validScalar(key) || !pub) {
            fail(error, "Invalid input key.");
            return std::nullopt;
        }
        if (input.taproot && static_cast<quint8>(pub->at(0)) == 0x03
            && secp256k1_ec_seckey_negate(ctx, bytes(key)) != 1) {
            fail(error, "Invalid input key.");
            return std::nullopt;
        }
        if (!sum) {
            sum = key;
        } else if (secp256k1_ec_seckey_tweak_add(ctx, bytes(*sum), bytes(key)) != 1) {
            sum.reset();   // the keys so far cancel out
        }
    }
    if (!anyCounts) {
        fail(error, "None of these coins can pay a silent payment address.");
        return std::nullopt;
    }
    if (!sum) {
        fail(error, "The inputs' keys sum to zero: pick other coins.");
        return std::nullopt;
    }
    const QByteArray a = *sum;
    const auto A = publicKey(a);

    // input_hash = hash_BIP0352/Inputs(smallest outpoint of all inputs || A)
    QByteArray smallest;
    for (const Input &input : inputs) {
        const QByteArray outpoint = serializeOutpoint(input);
        if (smallest.isEmpty() || outpoint < smallest) {
            smallest = outpoint;
        }
    }
    const auto inputHash = taggedHash("BIP0352/Inputs", smallest + *A);
    if (!inputHash || !validScalar(*inputHash)) {
        fail(error, "Silent payment derivation failed.");
        return std::nullopt;
    }
    QByteArray shared = a;   // input_hash · a
    if (secp256k1_ec_seckey_tweak_mul(ctx, bytes(shared), bytes(*inputHash)) != 1) {
        fail(error, "Silent payment derivation failed.");
        return std::nullopt;
    }

    // Recipients grouped by scan key, in order of first appearance.
    QList<QByteArray> scanKeys;
    QList<QList<int>> groups;
    for (int i = 0; i < recipients.size(); ++i) {
        const qsizetype g = scanKeys.indexOf(recipients[i].scanKey);
        if (g < 0) {
            scanKeys.append(recipients[i].scanKey);
            groups.append(QList<int>{i});
        } else {
            groups[g].append(i);
        }
    }

    QList<QByteArray> out(recipients.size());
    for (qsizetype g = 0; g < scanKeys.size(); ++g) {
        if (groups[g].size() > maxOutputsPerScanKey) {
            fail(error, "Too many outputs for one silent payment address.");
            return std::nullopt;
        }
        // ecdh_shared_secret = input_hash · a · B_scan
        secp256k1_pubkey scan;
        if (secp256k1_ec_pubkey_parse(ctx, &scan, bytes(scanKeys[g]), scanKeys[g].size()) != 1
            || secp256k1_ec_pubkey_tweak_mul(ctx, &scan, bytes(shared)) != 1) {
            fail(error, "Silent payment derivation failed.");
            return std::nullopt;
        }
        QByteArray secret(33, '\0');
        size_t len = secret.size();
        secp256k1_ec_pubkey_serialize(ctx, bytes(secret), &len, &scan, SECP256K1_EC_COMPRESSED);

        quint32 k = 0;
        for (int index : groups[g]) {
            // P_mk = B_m + hash_BIP0352/SharedSecret(secret || k) · G
            const auto t = taggedHash("BIP0352/SharedSecret", secret + bigEndian32(k));
            const QByteArray &spendKey = recipients[index].spendKey;
            QByteArray key(EC_PUBLIC_KEY_LEN, '\0');
            if (!t || !validScalar(*t)
                || wally_ec_public_key_tweak(bytes(spendKey), spendKey.size(), bytes(*t), t->size(),
                                             bytes(key), key.size()) != WALLY_OK) {
                fail(error, "Silent payment derivation failed.");
                return std::nullopt;
            }
            out[index] = key.mid(1);   // x-only
            ++k;
        }
    }
    return out;
}

QByteArray taprootScript(const QByteArray &xOnlyKey) {
    return QByteArray("\x51\x20", 2) + xOnlyKey;
}

}
