// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PublicDataFeed.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>

#include "PublicData.h"
#include "utils/config.h"
#include "utils/NetworkManager.h"
#include "utils/Networking.h"
#include "utils/TorManager.h"

namespace biscuit::datafeed {

PublicDataFeed::PublicDataFeed(QObject *parent)
    : QObject(parent)
{
    m_sources = {
        {cryptoRatesUrl(), cryptoRatesMessage, 10, new QTimer(this)},
        {fiatRatesUrl(), fiatRatesMessage, 60, new QTimer(this)},
        {crowdfundingUrl(), crowdfundingMessage, 60, new QTimer(this)},
        {newsUrl(), newsMessage, 360, new QTimer(this)},
    };
#if defined(CHECK_UPDATES)
    m_sources.append({updatesUrl(), updatesMessage, 360, new QTimer(this)});
#endif
    for (qsizetype i = 0; i < m_sources.size(); ++i) {
        m_sources[i].timer->setSingleShot(true);
        connect(m_sources[i].timer, &QTimer::timeout, this, [this, i] {
            Source &source = m_sources[i];
            // Waiting for Tor ("Tor only"): try again soon, not at the next interval.
            const bool sent = fetch(source);
            schedule(source, sent ? source.intervalMinutes * 60 * 1000 : 20 * 1000);
        });
    }
}

bool PublicDataFeed::allowedByProxySettings() {
    const int proxy = conf()->get(Config::proxy).toInt();
    if (proxy == Config::Proxy::i2p) {
        return false;
    }
    if (proxy == Config::Proxy::Tor && conf()->get(Config::torOnlyAllowOnion).toBool()) {
        return false;
    }
    return true;
}

void PublicDataFeed::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    // First fetch a few seconds after start, at a random moment.
    for (Source &source : m_sources) {
        schedule(source, 2000);
    }
}

void PublicDataFeed::stop() {
    m_running = false;
    for (Source &source : m_sources) {
        source.timer->stop();
    }
}

void PublicDataFeed::schedule(Source &source, int baseMs) {
    if (!m_running) {
        return;
    }
    const int jitterMs = QRandomGenerator::global()->bounded(60 * 1000);
    source.timer->start(baseMs + jitterMs);
}

bool PublicDataFeed::fetch(const Source &source) {
    if (!m_running || !allowedByProxySettings()) {
        return true;   // not waiting for anything: back at the usual interval
    }

    QNetworkReply *reply = nullptr;
    if (dataThroughTor()) {
        // "Tor only": through Tor or not at all, retried at the next interval
        // while Tor is still connecting.
        if (conf()->get(Config::offlineMode).toBool()) {
            return true;
        }
        if (!torManager()->torConnected) {
            return false;
        }
        QNetworkRequest request{QUrl(source.url)};
        request.setRawHeader("User-Agent", Networking(this).userAgent().toUtf8());
        reply = getNetworkDataTor()->get(request);
        reply->setParent(this);
    } else {
        Networking network{this};
        reply = network.getJson(this, source.url);
    }
    if (!reply) {
        return true;  // offline mode
    }

    const Converter convert = source.convert;
    connect(reply, &QNetworkReply::finished, this, [this, reply, convert] {
        reply->deleteLater();
        if (!m_running || reply->error() != QNetworkReply::NoError) {
            return;  // retried at the next interval
        }
        if (const auto msg = convert(reply->readAll())) {
            emit message(*msg);
        }
    });
    return true;
}

}
