// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include "utils/SemanticVersion.h"

class TestSemanticVersion : public QObject
{
Q_OBJECT

private slots:
    void parsesReleaseVersions() {
        auto v = SemanticVersion::fromString("2.9.1");
        QCOMPARE(v, SemanticVersion(2, 9, 1));
    }

    void parsesFourPartVersions() {
        auto v = SemanticVersion::fromString("2.9.1.3");
        QCOMPARE(v, SemanticVersion(2, 9, 1, 3));
    }

    void parsesBetaVersions() {
        auto v = SemanticVersion::fromString("beta-9");
        QCOMPARE(v, SemanticVersion(0, 9));
    }

    void ordersVersions() {
        QVERIFY(SemanticVersion(2, 10, 0) > SemanticVersion(2, 9, 1));
        QVERIFY(SemanticVersion(2, 9, 1) < SemanticVersion(3, 0, 0));
        QVERIFY(SemanticVersion(2, 9, 1) <= SemanticVersion(2, 9, 1));
        QVERIFY(SemanticVersion(2, 9, 1) != SemanticVersion(2, 9, 2));
    }
};

QTEST_APPLESS_MAIN(TestSemanticVersion)
#include "test_semanticversion.moc"
