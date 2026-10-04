// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_NETWORKMANAGER_H
#define FEATHER_NETWORKMANAGER_H

#include <QNetworkAccessManager>

QNetworkAccessManager* getNetworkSocks5();
QNetworkAccessManager* getNetworkClearnet();
// Biscuit: third-party data (prices, rates, news) in "Tor only" mode.
QNetworkAccessManager* getNetworkDataTor();
bool dataThroughTor();   // "Tor only" is set and the proxy is not Tor already
// Biscuit: whenever biscuitwallet.com is reached through Tor, its onion
// address is used, so the traffic never leaves the Tor network.
bool siteThroughOnion();
// biscuitwallet.com itself is allowed (not i2p, not "onion services only").
bool clearnetSiteAllowed();

QNetworkAccessManager* getNetwork(const QString &address = "");

#endif //FEATHER_NETWORKMANAGER_H
