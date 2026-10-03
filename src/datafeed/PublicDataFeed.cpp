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

void PublicDataFeed::schedule(Source &source, int baseMs) {
    if (!m_running) {
        return;
    }
    const int jitterMs = QRandomGenerator::global()->bounded(60 * 1000);
    source.timer->start(baseMs + jitterMs);
}

bool PublicDataFeed::fetch(Source &source) {
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

    Source *src = &source;
    connect(reply, &QNetworkReply::finished, this, [this, reply, src] {
        reply->deleteLater();
        if (!m_running) {
            return;
        }
        const auto msg = reply->error() == QNetworkReply::NoError ? src->convert(reply->readAll()) : std::nullopt;
        if (!msg) {
            // Failed: try again in a minute or two, not at the next interval.
            schedule(*src, 60 * 1000);
            return;
        }
        emit message(*msg);
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
    constexpr qint64 cacheMaxAgeSecs = 24 * 3600;
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
        if (source.cacheKey.isEmpty() || msg.isEmpty() || now - time > cacheMaxAgeSecs || time > now) {
            continue;
        }
        // After start() returns: listeners are connected by then.
        QTimer::singleShot(0, this, [this, msg] {
            if (m_running) emit message(msg);
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
