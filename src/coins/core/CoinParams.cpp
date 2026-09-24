// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinParams.h"

namespace biscuit::coins {

const CoinParams &bitcoin() {
    static const CoinParams p{"BTC", "Bitcoin", "bc", 0, 8, 0x00, {0x05}};
    return p;
}

const CoinParams &litecoin() {
    // 0x05 is the legacy P2SH version still accepted by Litecoin wallets.
    static const CoinParams p{"LTC", "Litecoin", "ltc", 2, 8, 0x30, {0x32, 0x05}};
    return p;
}

const CoinParams &bitcoinTestnet() {
    static const CoinParams p{"tBTC", "Bitcoin testnet", "tb", 1, 8, 0x6F, {0xC4}};
    return p;
}

const CoinParams &litecoinTestnet() {
    static const CoinParams p{"tLTC", "Litecoin testnet", "tltc", 1, 8, 0x6F, {0x3A, 0xC4}};
    return p;
}

}
