// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include <QtTest>

#include "ElectrumClient.h"

using biscuit::coins::ElectrumServer;

class TestElectrumServer : public QObject
{
Q_OBJECT

private slots:
    void parse() {
        auto s = ElectrumServer::parse("electrum.example.org:50002");
        QVERIFY(s && s->tls && s->host == "electrum.example.org" && s->port == 50002);
        s = ElectrumServer::parse("  SSL://Electrum.Example.org:443 ");
        QVERIFY(s && s->tls && s->host == "electrum.example.org" && s->port == 443);
        s = ElectrumServer::parse("electrum.example.org:50002:s");
        QVERIFY(s && s->tls);

        // Unencrypted: .onion and local network only.
        s = ElectrumServer::parse("tcp://abcdefghijklmnopqrstuvwxyz234567abcdefghijklmnopqrstuvwx.onion:50001");
        QVERIFY(s && !s->tls && s->isOnion());
        s = ElectrumServer::parse("tcp://192.168.1.20:50001");
        QVERIFY(s && !s->tls && s->isLocal());
        s = ElectrumServer::parse("umbrel.local:50001:t");
        QVERIFY(s && !s->tls && s->isLocal());
        QString error;
        QVERIFY(!ElectrumServer::parse("tcp://electrum.example.org:50001", &error));
        QVERIFY(error.contains("onion"));
        QVERIFY(!ElectrumServer::parse("electrum.example.org:50001:t"));

        // Local detection does not take public addresses.
        QVERIFY(!(ElectrumServer{"172.32.0.1", 50001, true}.isLocal()));
        QVERIFY((ElectrumServer{"172.20.0.1", 50001, true}.isLocal()));
        QVERIFY((ElectrumServer{"localhost", 50001, true}.isLocal()));

        // Hostnames that only look like private addresses are public: they
        // must go through the proxy and cannot use unencrypted TCP.
        for (const char *name : {"10.example.org", "127.0.0.1.example.org", "192.168.1.20.example.org",
                                 "localhost.example.org", "172.16.example.org"}) {
            QVERIFY2(!(ElectrumServer{name, 50001, true}.isLocal()), name);
        }
        QVERIFY(!ElectrumServer::parse(QString("tcp://10.example.org:50001")));
        QVERIFY((ElectrumServer{"127.0.0.1", 50001, true}.isLocal()));
        QVERIFY((ElectrumServer{"::1", 50001, true}.isLocal()));
        QVERIFY(!(ElectrumServer{"8.8.8.8", 50001, true}.isLocal()));

        // Malformed.
        for (const char *bad : {"", "example.org", "example.org:0", "example.org:70000", "example.org:abc",
                                "https://example.org/path:50002", ":50002"}) {
            QVERIFY2(!ElectrumServer::parse(bad), bad);
        }
    }
};

QTEST_GUILESS_MAIN(TestElectrumServer)
#include "test_electrum_server.moc"
