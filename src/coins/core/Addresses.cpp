// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Addresses.h"

#include <algorithm>

#include <wally_address.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_script.h>

#include "WallyInit.h"

namespace biscuit::coins {

namespace {
    const unsigned char *bytes(const QByteArray &b) {
        return reinterpret_cast<const unsigned char *>(b.constData());
    }
}

QByteArray p2wpkhScriptPubKey(const QByteArray &publicKey) {
    if (publicKey.size() != EC_PUBLIC_KEY_LEN) {
        return {};
    }
    ensureWallyInit();
    unsigned char script[WALLY_WITNESSSCRIPT_MAX_LEN];
    size_t written = 0;
    if (wally_witness_program_from_bytes(bytes(publicKey), publicKey.size(), WALLY_SCRIPT_HASH160,
                                         script, sizeof(script), &written) != WALLY_OK) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(script), static_cast<qsizetype>(written));
}

QString p2wpkhAddress(const QByteArray &publicKey, const CoinParams &params) {
    const QByteArray script = p2wpkhScriptPubKey(publicKey);
    if (script.isEmpty()) {
        return {};
    }
    char *address = nullptr;
    if (wally_addr_segwit_from_bytes(bytes(script), script.size(), params.bech32Hrp.toLatin1().constData(), 0, &address) != WALLY_OK) {
        return {};
    }
    const QString result = QString::fromLatin1(address);
    wally_free_string(address);
    return result;
}

std::optional<QByteArray> addressToScriptPubKey(const QString &address, const CoinParams &params) {
    ensureWallyInit();
    const QByteArray addr = address.trimmed().toLatin1();
    if (addr.isEmpty()) {
        return std::nullopt;
    }

    // Native SegWit: "<hrp>1…", bech32 (v0) or bech32m (v1+) is checked by libwally.
    const QByteArray hrp = params.bech32Hrp.toLatin1();
    if (addr.toLower().startsWith(hrp + '1')) {
        unsigned char script[WALLY_WITNESSSCRIPT_MAX_LEN];
        size_t written = 0;
        if (wally_addr_segwit_to_bytes(addr.constData(), hrp.constData(), 0, script, sizeof(script), &written) != WALLY_OK) {
            return std::nullopt;
        }
        return QByteArray(reinterpret_cast<const char *>(script), static_cast<qsizetype>(written));
    }

    // Legacy base58check: version byte + 20-byte hash.
    unsigned char decoded[1 + HASH160_LEN + BASE58_CHECKSUM_LEN];
    size_t written = 0;
    if (wally_base58_to_bytes(addr.constData(), BASE58_FLAG_CHECKSUM, decoded, sizeof(decoded), &written) != WALLY_OK
        || written != 1 + HASH160_LEN) {
        return std::nullopt;
    }
    const quint8 version = decoded[0];
    unsigned char script[WALLY_SCRIPTPUBKEY_P2PKH_LEN];
    int ret;
    if (version == params.p2pkhVersion) {
        ret = wally_scriptpubkey_p2pkh_from_bytes(decoded + 1, HASH160_LEN, 0, script, sizeof(script), &written);
    } else if (params.p2shVersions.contains(version)) {
        ret = wally_scriptpubkey_p2sh_from_bytes(decoded + 1, HASH160_LEN, 0, script, sizeof(script), &written);
    } else {
        return std::nullopt;
    }
    if (ret != WALLY_OK) {
        return std::nullopt;
    }
    return QByteArray(reinterpret_cast<const char *>(script), static_cast<qsizetype>(written));
}

QString electrumScriptHash(const QByteArray &scriptPubKey) {
    unsigned char hash[SHA256_LEN];
    if (wally_sha256(bytes(scriptPubKey), scriptPubKey.size(), hash, sizeof(hash)) != WALLY_OK) {
        return {};
    }
    std::reverse(std::begin(hash), std::end(hash));
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash), sizeof(hash)).toHex());
}

}
