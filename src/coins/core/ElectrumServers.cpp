// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "ElectrumServers.h"

namespace biscuit::coins {

QList<QPair<QString, quint16>> defaultElectrumServers(const CoinParams &params) {
    if (params == bitcoin()) {
        return {
            {"electrum.blockstream.info", 50002},
            {"electrum.emzy.de", 50002},
            {"fortress.qtornado.com", 443},
            {"electrum.bitaroo.net", 50002},
            {"bitcoin.lukechilds.co", 50002},
            {"electrum.acinq.co", 50002},
            {"electrum.diynodes.com", 50022},
        };
    }
    if (params == litecoin()) {
        return {
            {"electrum1.cipig.net", 20063},
            {"electrum2.cipig.net", 20063},
            {"electrum-ltc.bysh.me", 50002},
            {"backup.electrum-ltc.org", 443},
            {"electrum.ltc.xurious.com", 50002},
        };
    }
    return {};
}

}
