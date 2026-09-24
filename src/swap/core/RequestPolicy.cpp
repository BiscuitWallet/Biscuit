// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "RequestPolicy.h"

#include <algorithm>

namespace biscuit::swap::policy {

namespace {
    constexpr qint64 firstCheck = 5 * 60;
    constexpr qint64 activeInterval = 3 * 60;
    constexpr qint64 slowAfter = 2 * 60 * 60;
    constexpr qint64 slowInterval = 15 * 60;
}

qint64 statusPollInterval(qint64 tradeAgeSeconds) {
    return tradeAgeSeconds < slowAfter ? activeInterval : slowInterval;
}

bool isStatusCheckDue(qint64 tradeAgeSeconds, qint64 secondsSinceLastCheck, bool neverChecked) {
    if (tradeAgeSeconds < firstCheck) {
        return false;
    }
    if (neverChecked) {
        return true;
    }
    return secondsSinceLastCheck >= statusPollInterval(tradeAgeSeconds);
}

qint64 RateLimiter::reserve(qint64 nowMs) {
    // Forget requests that left the window.
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
                                 [&](qint64 t) { return t <= nowMs - m_window; }),
                  m_slots.end());

    qint64 sendAt = nowMs;
    if (m_slots.size() >= m_max) {
        // The window frees a slot when the oldest request in it expires.
        std::sort(m_slots.begin(), m_slots.end());
        sendAt = std::max(nowMs, m_slots.at(m_slots.size() - m_max) + m_window);
    }
    m_slots.append(sendAt);
    return sendAt - nowMs;
}

}
