// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PUBLICDATAFEED_H
#define BISCUIT_PUBLICDATAFEED_H

#include <functional>
#include <optional>

#include <QJsonObject>
#include <QObject>
#include <QTimer>

namespace biscuit::datafeed {

// Periodically fetches public data (see core/PublicData.h) and emits it as
// Feather websocket messages. Light: a few small requests per hour, with
// random jitter so the timing does not identify the user.
class PublicDataFeed : public QObject {
    Q_OBJECT

public:
    explicit PublicDataFeed(QObject *parent = nullptr);

    void start();
    void stop();

    // Clearnet sources cannot be reached when the user only allows onion
    // services or uses i2p: the feed stays off in that case.
    static bool allowedByProxySettings();

signals:
    void message(const QJsonObject &message);

private:
    using Converter = std::function<std::optional<QJsonObject>(const QByteArray &)>;

    struct Source {
        QString url;
        Converter convert;
        int intervalMinutes;
        QTimer *timer;
    };

    void fetch(const Source &source);
    void schedule(Source &source, int baseMs);

    QList<Source> m_sources;
    bool m_running = false;
};

}

#endif // BISCUIT_PUBLICDATAFEED_H
