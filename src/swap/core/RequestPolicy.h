// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_REQUESTPOLICY_H
#define BISCUIT_REQUESTPOLICY_H

#include <QList>
#include <QtGlobal>

// Request discipline agreed with swap partners (organic-demand usage):
//  - rates only when the user asks for offers, never polled;
//  - trade status on a slow schedule, see statusPollInterval();
//  - a global cap on requests per installation, see RateLimiter.
namespace biscuit::swap::policy {

    // Seconds to wait before checking a trade again, from its age:
    // first check 5 minutes after creation, then every 3 minutes, then every
    // 15 minutes once the trade is older than 2 hours.
    qint64 statusPollInterval(qint64 tradeAgeSeconds);

    // True if the trade status should be checked now.
    bool isStatusCheckDue(qint64 tradeAgeSeconds, qint64 secondsSinceLastCheck, bool neverChecked);

    // Sliding window limiter: at most `maxRequests` per `windowMs`.
    // Requests over the limit are delayed, never dropped or sent in bursts.
    class RateLimiter {
    public:
        RateLimiter(int maxRequests, qint64 windowMs) : m_max(maxRequests), m_window(windowMs) {}

        // Reserves a slot and returns how long to wait before sending (0 = now).
        qint64 reserve(qint64 nowMs);

    private:
        int m_max;
        qint64 m_window;
        QList<qint64> m_slots;   // send times of the requests in the window
    };
}

#endif // BISCUIT_REQUESTPOLICY_H
