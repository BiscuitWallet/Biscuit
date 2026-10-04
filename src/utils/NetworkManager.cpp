// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "NetworkManager.h"

#include <QCoreApplication>
#include <QNetworkProxy>
#include <QUrl>

#include "datafeed/core/PublicData.h"
#include "utils/config.h"
#include "utils/Utils.h"

QNetworkAccessManager *g_networkManagerSocks5 = nullptr;
QNetworkAccessManager *g_networkManagerClearnet = nullptr;

QNetworkAccessManager* getNetworkSocks5()
{
    if (!g_networkManagerSocks5) {
        g_networkManagerSocks5 = new QNetworkAccessManager(QCoreApplication::instance());
        QNetworkProxy proxy;
        proxy.setType(QNetworkProxy::Socks5Proxy);
        proxy.setHostName("127.0.0.1");
        proxy.setPort(9050);
        g_networkManagerSocks5->setProxy(proxy);
    }
    return g_networkManagerSocks5;
}

QNetworkAccessManager *g_networkManagerDataTor = nullptr;

QNetworkAccessManager* getNetworkDataTor()
{
    if (!g_networkManagerDataTor) {
        g_networkManagerDataTor = new QNetworkAccessManager(QCoreApplication::instance());
        // No route until WindowManager sets the Tor proxy: never clearnet.
        g_networkManagerDataTor->setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", 1));
    }
    return g_networkManagerDataTor;
}

bool dataThroughTor()
{
    return conf()->get(Config::dataTorOnly).toBool() && conf()->get(Config::proxy).toInt() != Config::Proxy::Tor;
}

bool siteThroughOnion()
{
    return conf()->get(Config::proxy).toInt() == Config::Proxy::Tor || dataThroughTor();
}

bool clearnetSiteAllowed()
{
    const int proxy = conf()->get(Config::proxy).toInt();
    return proxy != Config::Proxy::i2p && !(proxy == Config::Proxy::Tor && conf()->get(Config::torOnlyAllowOnion).toBool());
}

QNetworkAccessManager* getNetworkClearnet()
{
    if (!g_networkManagerClearnet) {
        g_networkManagerClearnet = new QNetworkAccessManager(QCoreApplication::instance());
    }
    return g_networkManagerClearnet;
}


QNetworkAccessManager* getNetwork(const QString &address)
{
    // Biscuit: in "Tor only" mode, whatever comes from biscuitwallet.com
    // (data, news, updates and their downloads) goes through Tor.
    const QString host = QUrl(address).host();
    if (dataThroughTor() && (host.endsWith(QLatin1String("biscuitwallet.com"))
                             || host == QUrl(biscuit::datafeed::onionSiteUrl()).host())) {
        return getNetworkDataTor();
    }

    if (conf()->get(Config::proxy).toInt() == Config::Proxy::None) {
        return getNetworkClearnet();
    }

    // Ignore proxy rules for local addresses
    if (!address.isEmpty() && Utils::isLocalUrl(QUrl(address))) {
        return getNetworkClearnet();
    }

    return getNetworkSocks5();
}