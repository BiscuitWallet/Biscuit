// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_AMOUNT_H
#define BISCUIT_SWAP_AMOUNT_H

#include <optional>

#include <QString>

// Exact decimal amounts for swaps.
// Amounts coming from swap APIs are kept as decimal strings and never go through
// floating point: comparisons and conversions to atomic units are done on digits.
namespace biscuit::swap::amount {

    // "12", "0.5", "1.000000000001" are valid. Signs, exponents, spaces and empty
    // integer/fractional parts (".5", "5.") are not.
    bool isValid(const QString &value);

    // Canonical form: no leading zeros in the integer part, no trailing zeros in
    // the fractional part, no trailing dot ("007.50" -> "7.5", "0.000" -> "0").
    // Returns an empty string for invalid input.
    QString normalize(const QString &value);

    // Returns -1, 0 or 1. Both values must be valid.
    int compare(const QString &a, const QString &b);

    bool isZero(const QString &value);

    // Converts to atomic units (e.g. piconero with decimals = 12, satoshi with 8).
    // Fails on invalid input, on more fractional digits than `decimals`
    // (no silent rounding) and on overflow.
    std::optional<quint64> toAtomic(const QString &value, int decimals);

    QString fromAtomic(quint64 atomic, int decimals);

    // JSON numbers from APIs (e.g. 0.123) are converted with enough precision
    // and normalized. Only for display and ranking, never for sending funds.
    QString fromDouble(double value);
}

#endif // BISCUIT_SWAP_AMOUNT_H
