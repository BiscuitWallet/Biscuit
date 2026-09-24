// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_HDACCOUNT_H
#define BISCUIT_HDACCOUNT_H

#include <memory>
#include <optional>

#include <QByteArray>
#include <QString>

#include "CoinParams.h"

struct ext_key;

namespace biscuit::coins {

// One BIP84 account (native SegWit, P2WPKH): m/84'/coin'/account'.
// Holds the account private key in memory; it is wiped on destruction.
class HdAccount {
public:
    enum Chain : quint32 {
        Receive = 0,
        Change = 1
    };

    // `seed` is the 64-byte BIP39 seed.
    static std::optional<HdAccount> fromSeed(const QByteArray &seed, const CoinParams &params, quint32 account = 0);

    HdAccount(HdAccount &&) noexcept;
    HdAccount &operator=(HdAccount &&) noexcept;
    ~HdAccount();

    const CoinParams &params() const { return *m_params; }

    // Compressed public key (33 bytes) of m/84'/coin'/account'/chain/index.
    QByteArray publicKey(Chain chain, quint32 index) const;
    // P2WPKH scriptPubKey: OP_0 <hash160(pubkey)>.
    QByteArray scriptPubKey(Chain chain, quint32 index) const;
    // bc1q… / ltc1q… address.
    QString address(Chain chain, quint32 index) const;

    // Private key (32 bytes) for signing. Callers must wipe it after use.
    QByteArray privateKey(Chain chain, quint32 index) const;

private:
    HdAccount(std::unique_ptr<ext_key> key, const CoinParams &params);
    bool derive(Chain chain, quint32 index, ext_key &out, bool withPrivate) const;

    std::unique_ptr<ext_key> m_key;
    const CoinParams *m_params;
};

}

#endif // BISCUIT_HDACCOUNT_H
