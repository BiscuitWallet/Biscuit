// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "TorCheck.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>

#include "PublicDataFeed.h"
#include "utils/config.h"
#include "utils/NetworkManager.h"
#include "utils/Networking.h"
#include "utils/TorManager.h"

namespace biscuit::datafeed {

namespace {
    const QString torCheckUrl = "https://biscuitwallet.com/data/tor-check";
    constexpr int intervalMs = 30 * 60 * 1000;
    constexpr int confirmMs = 2 * 60 * 1000;   // a "no" is asked again soon
}

TorCheck *TorCheck::instance() {
    static TorCheck *check = new TorCheck(QCoreApplication::instance());
    return check;
}

TorCheck::TorCheck(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &TorCheck::check);
    // TorManager repeats its state every few seconds: only a change counts.
    m_torConnected = torManager()->torConnected;
    connect(torManager(), &TorManager::connectionStateChanged, this, [this](bool connected) {
        if (connected != m_torConnected) {
            m_torConnected = connected;
            restart();
        }
    });
    schedule(10 * 1000);
}

bool TorCheck::enabled() {
    return conf()->get(Config::proxy).toInt() == Config::Proxy::Tor
        && !conf()->get(Config::disableWebsocket).toBool()
        && !conf()->get(Config::offlineMode).toBool()
        // biscuitwallet.com, not the onion: what is checked is that Biscuit's
        // traffic leaves the Tor network at an exit, which an onion cannot show.
        && clearnetSiteAllowed();
}

void TorCheck::restart() {
    ++m_generation;   // an answer still on its way is for the old settings
    setResult(Result::Unknown);
    schedule(10 * 1000);
}

void TorCheck::schedule(int ms) {
    // Random delay, so the timing does not identify the user.
    m_timer.start(ms + QRandomGenerator::global()->bounded(30 * 1000));
}

void TorCheck::check() {
    schedule(intervalMs);
    if (!enabled() || !torManager()->torConnected) {
        setResult(Result::Unknown);
        return;
    }

    // The same route as everything else in Tor mode: that is what is checked.
    Networking network{this};
    QNetworkReply *reply = network.getJson(this, torCheckUrl);
    if (!reply) {
        return;
    }
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation || !enabled()) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            return;   // no answer is not an answer: keep the last one
        }
        const QString answer = QJsonDocument::fromJson(reply->readAll()).object().value("tor").toString();
        if (answer == "yes") {
            setResult(Result::Tor);
        } else if (answer == "no") {
            setResult(Result::NotTor);
            schedule(confirmMs);
        } else {
            setResult(Result::Unknown);
        }
    });
}

void TorCheck::setResult(Result result) {
    if (result != m_result) {
        m_result = result;
        qInfo() << "Tor check:" << (result == Result::Tor ? "through Tor" : result == Result::NotTor ? "NOT through Tor" : "unknown");
        emit resultChanged(result);
    }
}

}
