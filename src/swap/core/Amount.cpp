// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Amount.h"

#include <limits>

namespace biscuit::swap::amount {

namespace {
    struct Parts {
        QString integer;
        QString fraction;
    };

    bool allDigits(const QString &s) {
        for (const QChar c : s) {
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
                return false;
            }
        }
        return true;
    }

    Parts split(const QString &normalized) {
        const qsizetype dot = normalized.indexOf(QLatin1Char('.'));
        if (dot < 0) {
            return {normalized, QString()};
        }
        return {normalized.left(dot), normalized.mid(dot + 1)};
    }
}

bool isValid(const QString &value) {
    if (value.isEmpty()) {
        return false;
    }
    const qsizetype dot = value.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        return allDigits(value);
    }
    if (value.indexOf(QLatin1Char('.'), dot + 1) >= 0) {
        return false;
    }
    const QString integer = value.left(dot);
    const QString fraction = value.mid(dot + 1);
    return !integer.isEmpty() && !fraction.isEmpty() && allDigits(integer) && allDigits(fraction);
}

QString normalize(const QString &value) {
    if (!isValid(value)) {
        return {};
    }

    Parts parts = split(value);

    qsizetype firstNonZero = 0;
    while (firstNonZero < parts.integer.size() - 1 && parts.integer.at(firstNonZero) == QLatin1Char('0')) {
        firstNonZero++;
    }
    parts.integer = parts.integer.mid(firstNonZero);

    qsizetype end = parts.fraction.size();
    while (end > 0 && parts.fraction.at(end - 1) == QLatin1Char('0')) {
        end--;
    }
    parts.fraction.truncate(end);

    if (parts.fraction.isEmpty()) {
        return parts.integer;
    }
    return parts.integer + QLatin1Char('.') + parts.fraction;
}

int compare(const QString &a, const QString &b) {
    const Parts pa = split(normalize(a));
    const Parts pb = split(normalize(b));

    // Normalized integer parts have no leading zeros: longer means bigger.
    if (pa.integer.size() != pb.integer.size()) {
        return pa.integer.size() < pb.integer.size() ? -1 : 1;
    }
    if (const int c = pa.integer.compare(pb.integer); c != 0) {
        return c < 0 ? -1 : 1;
    }

    const qsizetype width = std::max(pa.fraction.size(), pb.fraction.size());
    const QString fa = pa.fraction.leftJustified(width, QLatin1Char('0'));
    const QString fb = pb.fraction.leftJustified(width, QLatin1Char('0'));
    if (const int c = fa.compare(fb); c != 0) {
        return c < 0 ? -1 : 1;
    }
    return 0;
}

bool isZero(const QString &value) {
    return normalize(value) == QLatin1String("0");
}

std::optional<quint64> toAtomic(const QString &value, int decimals) {
    if (decimals < 0 || decimals > 19) {
        return std::nullopt;
    }
    const QString normalized = normalize(value);
    if (normalized.isEmpty()) {
        return std::nullopt;
    }

    const Parts parts = split(normalized);
    if (parts.fraction.size() > decimals) {
        return std::nullopt;
    }

    const QString digits = parts.integer + parts.fraction.leftJustified(decimals, QLatin1Char('0'));

    quint64 result = 0;
    constexpr quint64 max = std::numeric_limits<quint64>::max();
    for (const QChar c : digits) {
        const quint64 digit = c.unicode() - '0';
        if (result > (max - digit) / 10) {
            return std::nullopt;
        }
        result = result * 10 + digit;
    }
    return result;
}

QString fromAtomic(quint64 atomic, int decimals) {
    QString digits = QString::number(atomic);
    if (decimals <= 0) {
        return digits;
    }
    if (digits.size() <= decimals) {
        digits = digits.rightJustified(decimals + 1, QLatin1Char('0'));
    }
    const QString raw = digits.left(digits.size() - decimals) + QLatin1Char('.') + digits.right(decimals);
    return normalize(raw);
}

QString fromDouble(double value) {
    if (!(value >= 0) || value > 1e15) {
        return {};
    }
    return normalize(QString::number(value, 'f', 12));
}

}
