// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_DEMOSWAP_H
#define BISCUIT_SWAP_DEMOSWAP_H

#include <QDateTime>
#include <QList>

#include "SwapTypes.h"

// Offline demo provider, used when the app is built without a Trocador API key.
// Lets the whole swap flow be developed and tested without any network or key.
//
// Safety: demo deposit addresses are not valid on any chain ("DEMO-" prefix,
// '-' is not a base58 character), so no wallet can send funds to them.
namespace biscuit::swap::demo {

    inline constexpr char providerId[] = "demo";
    inline constexpr char depositAddressPrefix[] = "DEMO-";

    QList<AssetInfo> assets();

    // Same request, same quotes. Unsupported pairs return an empty list.
    QList<Quote> quotes(const QuoteRequest &request);

    Trade createTrade(const TradeRequest &request, const QDateTime &now);

    // Simulated progress: waiting, then confirming after 1 min, sending after
    // 2 min, finished after 3 min. Exchanges whose name contains "Halt" halt.
    TradeStatus statusAt(const Trade &trade, const QDateTime &now);

    bool isDemoAddress(const QString &address);
}

#endif // BISCUIT_SWAP_DEMOSWAP_H
