// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PublicDataFeed.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>

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
        {cryptoRatesUrl(), cryptoRatesMessage, 10, new QTimer(this), "crypto"},
        {fiatRatesUrl(), fiatRatesMessage, 60, new QTimer(this), "fiat"},
        {crowdfundingUrl(), crowdfundingMessage, 60, new QTimer(this), "ccs", 7},
        {newsUrl(), newsMessage, 360, new QTimer(this), "news", 7},
    };
#if defined(CHECK_UPDATES)
    m_sources.append({updatesUrl(), updatesMessage, 360, new QTimer(this)});
#endif
    for (qsizetype i = 0; i < m_sources.size(); ++i) {
        m_sources[i].timer->setSingleShot(true);
        connect(m_sources[i].timer, &QTimer::timeout, this, [this, i] {
            Source &source = m_sources[i];
            // Waiting for Tor: fetched as soon as Tor is ready (onTorConnected), this
            // later try only covers a missed signal.
            const bool sent = fetch(source, siteThroughOnion());
            source.waitingForTor = !sent;
            schedule(source, sent ? source.intervalMinutes * 60 * 1000 : 60 * 1000);
        });
    }
    // TorManager repeats its state every few seconds: only the change counts.
    m_torConnected = torManager()->torConnected;
    connect(torManager(), &TorManager::connectionStateChanged, this, [this](bool connected) {
        const bool became = connected && !m_torConnected;
        m_torConnected = connected;
        if (became) {
            onTorConnected();
        }
    });
}

void PublicDataFeed::onTorConnected() {
    // A few seconds of random delay still, so the requests do not leave together.
    for (Source &source : m_sources) {
        if (source.waitingForTor) {
            source.waitingForTor = false;
            schedule(source, 1000, 10 * 1000);
        }
    }
}

bool PublicDataFeed::allowedByProxySettings() {
    // Through Tor the onion service is used, so "onion services only" is fine.
    return conf()->get(Config::proxy).toInt() != Config::Proxy::i2p;
}

void PublicDataFeed::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    loadCache();
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

void PublicDataFeed::schedule(Source &source, int baseMs, int jitterMs) {
    if (!m_running) {
        return;
    }
    source.timer->start(baseMs + int(QRandomGenerator::global()->bounded(quint32(jitterMs))));
}

bool PublicDataFeed::fetch(Source &source, bool viaOnion) {
    if (!m_running || !allowedByProxySettings()) {
        return true;   // not waiting for anything: back at the usual interval
    }

    const QString url = viaOnion ? onionUrl(source.url) : source.url;
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
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("User-Agent", Networking(this).userAgent().toUtf8());
        reply = getNetworkDataTor()->get(request);
        reply->setParent(this);
    } else {
        // Tor mode: wait for Tor, instead of a request that fails and waits a minute.
        if (conf()->get(Config::proxy).toInt() == Config::Proxy::Tor && !torManager()->torConnected) {
            return false;
        }
        Networking network{this};
        reply = network.getJson(this, url);
    }
    if (!reply) {
        return true;  // offline mode
    }

    Source *src = &source;
    connect(reply, &QNetworkReply::finished, this, [this, reply, src, viaOnion] {
        reply->deleteLater();
        if (!m_running) {
            return;
        }
        const auto msg = reply->error() == QNetworkReply::NoError ? src->convert(reply->readAll()) : std::nullopt;
        if (!msg && viaOnion && clearnetSiteAllowed()) {
            fetch(*src, false);
            return;
        }
        if (!msg) {
            // Failed: try again in a minute or two, not at the next interval.
            schedule(*src, 60 * 1000);
            return;
        }
        QJsonObject fresh = *msg;
        fresh["time"] = double(QDateTime::currentSecsSinceEpoch());
        emit message(fresh);
        if (!src->cacheKey.isEmpty()) {
            saveCache(src->cacheKey, *msg);
        }
    });
    return true;
}

namespace {
    QString cachePath() {
        return Config::defaultConfigDir().filePath("datafeed-cache.json");
    }
}

void PublicDataFeed::loadCache() {
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject cache = QJsonDocument::fromJson(file.readAll()).object();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const Source &source : m_sources) {
        const QJsonObject entry = cache.value(source.cacheKey).toObject();
        const qint64 time = qint64(entry.value("time").toDouble());
        const QJsonObject msg = entry.value("message").toObject();
        if (source.cacheKey.isEmpty() || msg.isEmpty() || now - time > source.cacheDays * 24 * 3600 || time > now) {
            continue;
        }
        // After start() returns: listeners are connected by then. The copy keeps
        // the time it was fetched, so it is never mistaken for fresh data.
        QJsonObject cached = msg;
        cached["time"] = double(time);
        QTimer::singleShot(0, this, [this, cached] {
            if (m_running) emit message(cached);
        });
    }
}

void PublicDataFeed::saveCache(const QString &key, const QJsonObject &message) {
    QFile file(cachePath());
    QJsonObject cache;
    if (file.open(QIODevice::ReadOnly)) {
        cache = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }
    cache.insert(key, QJsonObject{{"time", double(QDateTime::currentSecsSinceEpoch())}, {"message", message}});
    QSaveFile out(cachePath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
        out.commit();
    }
}

}
