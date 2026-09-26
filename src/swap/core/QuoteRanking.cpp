// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "QuoteRanking.h"

#include <algorithm>

#include "Amount.h"

namespace biscuit::swap {

QList<Quote> rankQuotes(const QList<Quote> &quotes, KycRating minKycRating) {
    QList<Quote> ranked;
    for (const Quote &quote : quotes) {
        if (!amount::isValid(quote.amountTo) || amount::isZero(quote.amountTo)
            || !amount::isValid(quote.amountFrom) || amount::isZero(quote.amountFrom)) {
            continue;
        }
        if (!kycRatingAtLeast(quote.kycRating, minKycRating)) {
            continue;
        }
        ranked.append(quote);
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const Quote &a, const Quote &b) {
        // Floating: most received first. Fixed (same amount received): least sent first.
        if (a.rateType == RateType::Fixed && b.rateType == RateType::Fixed) {
            if (const int c = amount::compare(a.amountFrom, b.amountFrom); c != 0) {
                return c < 0;
            }
        } else if (const int c = amount::compare(a.amountTo, b.amountTo); c != 0) {
            return c > 0;
        }
        if (a.etaMinutes.has_value() != b.etaMinutes.has_value()) {
            return a.etaMinutes.has_value();
        }
        if (a.etaMinutes && b.etaMinutes && *a.etaMinutes != *b.etaMinutes) {
            return *a.etaMinutes < *b.etaMinutes;
        }
        if (const int c = a.exchange.compare(b.exchange, Qt::CaseInsensitive); c != 0) {
            return c < 0;
        }
        return a.providerId < b.providerId;
    });

    return ranked;
}

}
