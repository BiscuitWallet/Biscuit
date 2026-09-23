// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_QUOTERANKING_H
#define BISCUIT_SWAP_QUOTERANKING_H

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

}

#endif // BISCUIT_SWAP_QUOTERANKING_H
