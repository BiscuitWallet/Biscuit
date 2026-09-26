// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_QUOTERANKING_H
#define BISCUIT_SWAP_QUOTERANKING_H

#include <optional>

#include <QList>

#include "SwapTypes.h"

namespace biscuit::swap {

// Neutral ranking of quotes, best first.
//
// Criteria, in order:
//   1. amount received (higher is better),
//   2. ETA (shorter is better, unknown ETA last),
//   3. exchange name then provider id (only to make the order deterministic).
//
// The order returned by the APIs is ignored and nothing related to partner
// revenue is taken into account (Quote has no such field on purpose).
//
// Quotes with an invalid or zero amount, or below `minKycRating`, are dropped.
QList<Quote> rankQuotes(const QList<Quote> &quotes, KycRating minKycRating);

// Display only, never used for ranking (ranking depends on the amount
// received alone). Cost of a swap compared with the market price: the value
// sent minus the value received, in percent of the value sent, both valued
// with Biscuit's public price data. 0.9 = the swap costs 0.9% of what you
// send (exchange fee, spread, network fee). Negative when the offer beats the
// reference price (usually a slightly stale reference). No value without
// usable prices.
std::optional<double> swapCostPercent(double valueSent, double valueReceived);
// "0.9%", "12%", "-0.2%".
QString formatCostPercent(double percent);
// High enough to point out.
bool swapCostNeedsWarning(double percent);

}

#endif // BISCUIT_SWAP_QUOTERANKING_H
