// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Request discipline promised to Trocador: these numbers are a commitment.

#include <QtTest>

#include "RequestPolicy.h"

using namespace biscuit::swap::policy;

class TestRequestPolicy : public QObject
{
Q_OBJECT

private slots:
    void statusSchedule() {
        // Nothing during the first 5 minutes (the user is sending the deposit).
        QVERIFY(!isStatusCheckDue(0, 0, true));
        QVERIFY(!isStatusCheckDue(299, 0, true));
        QVERIFY(isStatusCheckDue(300, 0, true));
        // Then every 3 minutes.
        QVERIFY(!isStatusCheckDue(600, 179, false));
        QVERIFY(isStatusCheckDue(600, 180, false));
        // After 2 hours, every 15 minutes.
        QVERIFY(!isStatusCheckDue(2 * 3600, 800, false));
        QVERIFY(isStatusCheckDue(2 * 3600, 900, false));
    }

    void typicalSwapStaysWithinBudget() {
        // A 30-minute swap polled on this schedule: 1 check at 5 min, then
        // every 3 minutes until 30 min -> at most 9 status requests.
        int checks = 0;
        qint64 last = -1;
        for (qint64 age = 0; age <= 30 * 60; age += 10) {
            if (isStatusCheckDue(age, last < 0 ? 0 : age - last, last < 0)) {
                checks++;
                last = age;
            }
        }
        QVERIFY(checks <= 9);
        QVERIFY(checks >= 8);
    }

    void rateLimiterDelaysInsteadOfBursting() {
        RateLimiter limiter(3, 60000);
        QCOMPARE(limiter.reserve(0), 0);
        QCOMPARE(limiter.reserve(1000), 0);
        QCOMPARE(limiter.reserve(2000), 0);
        // 4th request in the same minute waits until the first one expires.
        QCOMPARE(limiter.reserve(3000), 57000);
        // 5th waits for the second slot.
        QCOMPARE(limiter.reserve(3000), 58000);
        // Later, the window is free again.
        QCOMPARE(limiter.reserve(200000), 0);
    }

    void rateLimiterNeverExceedsTheLimit() {
        RateLimiter limiter(5, 60000);
        QList<qint64> sendTimes;
        for (int i = 0; i < 40; ++i) {
            const qint64 now = i * 500;   // a burst: 40 requests in 20 s
            sendTimes << now + limiter.reserve(now);
        }
        std::sort(sendTimes.begin(), sendTimes.end());
        for (int i = 5; i < sendTimes.size(); ++i) {
            QVERIFY2(sendTimes.at(i) - sendTimes.at(i - 5) >= 60000, "more than 5 requests in a minute");
        }
    }
};

QTEST_APPLESS_MAIN(TestRequestPolicy)
#include "test_request_policy.moc"
