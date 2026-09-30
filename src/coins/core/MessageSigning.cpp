// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MessageSigning.h"

#include <wally_address.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_script.h>

#include "Addresses.h"
#include "WallyInit.h"

namespace biscuit::coins::message {

namespace {
    const unsigned char *bytes(const QByteArray &data) {
        return reinterpret_cast<const unsigned char *>(data.constData());
    }

    // Bitcoin's CompactSize length prefix.
    QByteArray compactSize(quint64 n) {
        QByteArray out;
        auto le = [&out](quint64 v, int count) {
            for (int i = 0; i < count; ++i) out.append(char((v >> (8 * i)) & 0xff));
        };
        if (n < 0xfd) {
            out.append(char(n));
        } else if (n <= 0xffff) {
            out.append(char(0xfd));
            le(n, 2);
        } else if (n <= 0xffffffff) {
            out.append(char(0xfe));
            le(n, 4);
        } else {
            out.append(char(0xff));
            le(n, 8);
        }
        return out;
    }

    QString base58Check(const QByteArray &payload) {
        char *out = nullptr;
        if (wally_base58_from_bytes(bytes(payload), payload.size(), BASE58_FLAG_CHECKSUM, &out) != WALLY_OK) {
            return {};
        }
        const QString result = QString::fromLatin1(out);
        wally_free_string(out);
        return result;
    }

    QByteArray hash160(const QByteArray &data) {
        QByteArray out(HASH160_LEN, '\0');
        wally_hash160(bytes(data), data.size(), reinterpret_cast<unsigned char *>(out.data()), out.size());
        return out;
    }

    QByteArray decompress(const QByteArray &compressed) {
        QByteArray out(EC_PUBLIC_KEY_UNCOMPRESSED_LEN, '\0');
        if (wally_ec_public_key_decompress(bytes(compressed), compressed.size(),
                                           reinterpret_cast<unsigned char *>(out.data()), out.size()) != WALLY_OK) {
            return {};
        }
        return out;
    }
}

QByteArray hash(const QString &message, const CoinParams &params) {
    ensureWallyInit();
    const QByteArray prefix = QString("%1 Signed Message:\n").arg(params.name).toUtf8();
    const QByteArray text = message.toUtf8();
    const QByteArray data = compactSize(prefix.size()) + prefix + compactSize(text.size()) + text;
    QByteArray out(SHA256_LEN, '\0');
    wally_sha256d(bytes(data), data.size(), reinterpret_cast<unsigned char *>(out.data()), out.size());
    return out;
}

QString sign(const QByteArray &privateKey, const QString &message, const CoinParams &params) {
    ensureWallyInit();
    const QByteArray digest = hash(message, params);
    QByteArray sig(EC_SIGNATURE_RECOVERABLE_LEN, '\0');
    if (wally_ec_sig_from_bytes(bytes(privateKey), privateKey.size(), bytes(digest), digest.size(),
                                EC_FLAG_ECDSA | EC_FLAG_RECOVERABLE,
                                reinterpret_cast<unsigned char *>(sig.data()), sig.size()) != WALLY_OK) {
        return {};
    }
    return QString::fromLatin1(sig.toBase64());
}

bool verify(const QString &address, const QString &message, const QString &signature, const CoinParams &params) {
    ensureWallyInit();
    const QByteArray sig = QByteArray::fromBase64(signature.trimmed().toLatin1());
    if (sig.size() != EC_SIGNATURE_RECOVERABLE_LEN) {
        return false;
    }
    const quint8 header = quint8(sig.at(0));
    if (header < 27 || header > 42) {
        return false;
    }
    const QByteArray digest = hash(message, params);
    QByteArray pub(EC_PUBLIC_KEY_LEN, '\0');
    if (wally_ec_sig_to_public_key(bytes(digest), digest.size(), bytes(sig), sig.size(),
                                   reinterpret_cast<unsigned char *>(pub.data()), pub.size()) != WALLY_OK) {
        return false;
    }

    // Every address this key can have: whatever the header says, the
    // recovered key must be the one behind the given address.
    const QString given = address.trimmed();
    const QByteArray pkh = hash160(pub);
    if (given.compare(p2wpkhAddress(pub, params), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (given == base58Check(QByteArray(1, char(params.p2pkhVersion)) + pkh)) {
        return true;
    }
    if (header < 31) {   // signed with the uncompressed key
        const QByteArray full = decompress(pub);
        if (!full.isEmpty() && given == base58Check(QByteArray(1, char(params.p2pkhVersion)) + hash160(full))) {
            return true;
        }
    }
    const QByteArray redeem = QByteArray("\x00\x14", 2) + pkh;   // P2SH-P2WPKH
    for (quint8 version : params.p2shVersions) {
        if (given == base58Check(QByteArray(1, char(version)) + hash160(redeem))) {
            return true;
        }
    }
    return false;
}

}
