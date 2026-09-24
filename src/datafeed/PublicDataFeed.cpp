// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PublicDataFeed.h"

#include <QNetworkReply>
#include <QRandomGenerator>

#include "PublicData.h"
#include "utils/config.h"
#include "utils/Networking.h"

namespace biscuit::datafeed {

PublicDataFeed::PublicDataFeed(QObject *parent)
    : QObject(parent)
{
    m_sources = {
        {cryptoRatesUrl(), cryptoRatesMessage, 10, new QTimer(this)},
        {fiatRatesUrl(), fiatRatesMessage, 60, new QTimer(this)},
        {crowdfundingUrl(), crowdfundingMessage, 60, new QTimer(this)},
    };
    for (qsizetype i = 0; i < m_sources.size(); ++i) {
        m_sources[i].timer->setSingleShot(true);
        connect(m_sources[i].timer, &QTimer::timeout, this, [this, i] {
            Source &source = m_sources[i];
            fetch(source);
            schedule(source, source.intervalMinutes * 60 * 1000);
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

void PublicDataFeed::fetch(const Source &source) {
    if (!m_running || !allowedByProxySettings()) {
        return;
    }

    Networking network{this};
    QNetworkReply *reply = network.getJson(this, source.url);
    if (!reply) {
        return;  // offline mode
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
}

}
