// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include "Amount.h"

using namespace biscuit::swap;

class TestAmount : public QObject
{
Q_OBJECT

private slots:
    void validation_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("valid");
        QTest::newRow("integer") << "12" << true;
        QTest::newRow("decimal") << "0.5" << true;
        QTest::newRow("many decimals") << "1.000000000001" << true;
        QTest::newRow("leading zeros") << "007.50" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("negative") << "-1" << false;
        QTest::newRow("plus") << "+1" << false;
        QTest::newRow("exponent") << "1e5" << false;
        QTest::newRow("comma") << "1,5" << false;
        QTest::newRow("space") << " 1" << false;
        QTest::newRow("no integer part") << ".5" << false;
        QTest::newRow("no fraction part") << "5." << false;
        QTest::newRow("two dots") << "1.2.3" << false;
        QTest::newRow("letters") << "abc" << false;
    }
    void validation() {
        QFETCH(QString, value);
        QFETCH(bool, valid);
        QCOMPARE(amount::isValid(value), valid);
    }

    void normalize() {
        QCOMPARE(amount::normalize("007.50"), QString("7.5"));
        QCOMPARE(amount::normalize("0.000"), QString("0"));
        QCOMPARE(amount::normalize("000"), QString("0"));
        QCOMPARE(amount::normalize("10"), QString("10"));
        QCOMPARE(amount::normalize("1.0"), QString("1"));
        QCOMPARE(amount::normalize("bad"), QString());
    }

    void compare() {
        QCOMPARE(amount::compare("1", "1.0"), 0);
        QCOMPARE(amount::compare("0.1", "0.09"), 1);
        QCOMPARE(amount::compare("0.09", "0.1"), -1);
        QCOMPARE(amount::compare("10", "9.999999"), 1);
        QCOMPARE(amount::compare("2", "10"), -1);
        QCOMPARE(amount::compare("0.123456789012", "0.123456789013"), -1);
        // Precision beyond double: must still be exact.
        QCOMPARE(amount::compare("1.0000000000000000001", "1"), 1);
    }

    void toAtomicMonero() {
        QCOMPARE(amount::toAtomic("1", 12), std::optional<quint64>(1000000000000ULL));
        QCOMPARE(amount::toAtomic("0.000000000001", 12), std::optional<quint64>(1ULL));
        QCOMPARE(amount::toAtomic("12.345", 12), std::optional<quint64>(12345000000000ULL));
        QCOMPARE(amount::toAtomic("0", 12), std::optional<quint64>(0ULL));
    }

    void toAtomicBitcoin() {
        QCOMPARE(amount::toAtomic("0.00000001", 8), std::optional<quint64>(1ULL));
        QCOMPARE(amount::toAtomic("21000000", 8), std::optional<quint64>(2100000000000000ULL));
    }

    void toAtomicRejectsRounding() {
        // More decimals than the coin supports: refuse instead of rounding.
        QVERIFY(!amount::toAtomic("0.0000000000001", 12).has_value());
        QVERIFY(!amount::toAtomic("0.000000001", 8).has_value());
        // Trailing zeros are fine.
        QCOMPARE(amount::toAtomic("0.100000000000000", 8), std::optional<quint64>(10000000ULL));
    }

    void toAtomicRejectsOverflowAndInvalid() {
        QVERIFY(!amount::toAtomic("18446744073709551616", 0).has_value());
        QCOMPARE(amount::toAtomic("18446744073709551615", 0), std::optional<quint64>(18446744073709551615ULL));
        QVERIFY(!amount::toAtomic("100000000", 12).has_value());
        QVERIFY(!amount::toAtomic("-1", 12).has_value());
        QVERIFY(!amount::toAtomic("", 12).has_value());
    }

    void fromAtomic() {
        QCOMPARE(amount::fromAtomic(1000000000000ULL, 12), QString("1"));
        QCOMPARE(amount::fromAtomic(1ULL, 12), QString("0.000000000001"));
        QCOMPARE(amount::fromAtomic(150000000ULL, 8), QString("1.5"));
        QCOMPARE(amount::fromAtomic(0ULL, 8), QString("0"));
    }

    void roundTrip() {
        for (const QString &v : {"0.5", "123.456789012345", "0.000000000001", "42"}) {
            const auto atomic = amount::toAtomic(v, 12);
            QVERIFY(atomic.has_value());
            QCOMPARE(amount::fromAtomic(*atomic, 12), amount::normalize(v));
        }
    }

    void fromDouble() {
        QCOMPARE(amount::fromDouble(0.5), QString("0.5"));
        QCOMPARE(amount::fromDouble(12.0), QString("12"));
        QCOMPARE(amount::fromDouble(-1.0), QString());
    }
};

QTEST_APPLESS_MAIN(TestAmount)
#include "test_amount.moc"
