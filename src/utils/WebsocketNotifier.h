// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_WEBSOCKETNOTIFIER_H
#define FEATHER_WEBSOCKETNOTIFIER_H

#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QMap>

#include "WebsocketClient.h"
#include "networktype.h"
#include "nodes.h"
#include "prices.h"

class WebsocketNotifier : public QObject {
    Q_OBJECT

public:
    explicit WebsocketNotifier(QObject *parent);
    ~WebsocketNotifier();

    QMap<NetworkType::Type, int> heights;
    WebsocketClient *websocketClient;

    static WebsocketNotifier* instance();
    void emitCache();

    // How old the last data of this kind is, in seconds (-1: none yet).
    qint64 secondsSince(const QString &cmd) const;

signals:
    void BlockHeightsReceived(int mainnet, int stagenet);
    void NodesReceived(QList<FeatherNode> &L);
    void CryptoRatesReceived(const QJsonArray &data);
    void FiatRatesReceived(const QJsonObject &fiat_rates);
    void UpdatesReceived(const QJsonObject &updates);
    void dataReceived(const QString &type, const QJsonValue &json);

private slots:
    void onWSMessage(const QJsonObject &msg);

    void onWSNodes(const QJsonArray &nodes);
    void onWSUpdates(const QJsonObject &updates);

private:
    static QPointer<WebsocketNotifier> m_instance;

    QStringList m_pluginSubscriptions;
    QHash<QString, QJsonObject> m_cache;
    QHash<QString, QDateTime> m_received;
};

inline WebsocketNotifier* websocketNotifier()
{
    return WebsocketNotifier::instance();
}


#endif //FEATHER_WEBSOCKETNOTIFIER_H