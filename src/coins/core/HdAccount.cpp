// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "HdAccount.h"

#include <wally_bip32.h>
#include <wally_core.h>

#include "Addresses.h"
#include "WallyInit.h"

namespace biscuit::coins {

namespace {
    constexpr quint32 hardened(quint32 i) { return i | BIP32_INITIAL_HARDENED_CHILD; }
}

std::optional<HdAccount> HdAccount::fromSeed(const QByteArray &seed, const CoinParams &params, quint32 account) {
    ensureWallyInit();

    ext_key master;
    if (bip32_key_from_seed(reinterpret_cast<const unsigned char *>(seed.constData()), seed.size(),
                            BIP32_VER_MAIN_PRIVATE, 0, &master) != WALLY_OK) {
        return std::nullopt;
    }

    const uint32_t path[] = {hardened(84), hardened(params.bip44CoinType), hardened(account)};
    auto key = std::make_unique<ext_key>();
    const int ret = bip32_key_from_parent_path(&master, path, 3, BIP32_FLAG_KEY_PRIVATE, key.get());
    wally_bzero(&master, sizeof(master));
    if (ret != WALLY_OK) {
        wally_bzero(key.get(), sizeof(ext_key));
        return std::nullopt;
    }
    return HdAccount(std::move(key), params);
}

HdAccount::HdAccount(std::unique_ptr<ext_key> key, const CoinParams &params)
    : m_key(std::move(key))
    , m_params(&params)
{
}

HdAccount::HdAccount(HdAccount &&) noexcept = default;
HdAccount &HdAccount::operator=(HdAccount &&) noexcept = default;

HdAccount::~HdAccount() {
    if (m_key) {
        wally_bzero(m_key.get(), sizeof(ext_key));
    }
}

bool HdAccount::derive(Chain chain, quint32 index, ext_key &out, bool withPrivate) const {
    const uint32_t path[] = {static_cast<uint32_t>(chain), index};
    const uint32_t flags = withPrivate ? BIP32_FLAG_KEY_PRIVATE : (BIP32_FLAG_KEY_PUBLIC | BIP32_FLAG_SKIP_HASH);
    return bip32_key_from_parent_path(m_key.get(), path, 2, flags, &out) == WALLY_OK;
}

QByteArray HdAccount::publicKey(Chain chain, quint32 index) const {
    ext_key child;
    if (!derive(chain, index, child, false)) {
        return {};
    }
    QByteArray pub(reinterpret_cast<const char *>(child.pub_key), sizeof(child.pub_key));
    wally_bzero(&child, sizeof(child));
    return pub;
}

QByteArray HdAccount::scriptPubKey(Chain chain, quint32 index) const {
    return p2wpkhScriptPubKey(publicKey(chain, index));
}

QString HdAccount::address(Chain chain, quint32 index) const {
    return p2wpkhAddress(publicKey(chain, index), *m_params);
}

QByteArray HdAccount::privateKey(Chain chain, quint32 index) const {
    ext_key child;
    if (!derive(chain, index, child, true)) {
        return {};
    }
    // priv_key[0] is a 0x00 prefix byte, the key is the 32 bytes after it.
    QByteArray priv(reinterpret_cast<const char *>(child.priv_key + 1), 32);
    wally_bzero(&child, sizeof(child));
    return priv;
}

}
