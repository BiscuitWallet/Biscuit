// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicEvents.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Amount.h"

namespace biscuit::swap::atomic {

namespace {
    quint64 toSat(const QJsonValue &value) {
        const double v = value.toDouble(-1);
        return v > 0 ? static_cast<quint64>(v) : 0;
    }

    MakerOffer parseOffer(const QJsonObject &o) {
        MakerOffer offer;
        offer.peerId = o.value("peer_id").toString();
        offer.address = o.value("address").toString();
        offer.version = o.value("version").toString();
        offer.priceSatPerXmr = toSat(o.value("price_sat_per_xmr"));
        offer.minSat = toSat(o.value("min_sat"));
        offer.maxSat = toSat(o.value("max_sat"));

        const QJsonObject policy = o.value("refund_policy").toObject();
        if (policy.value("type").toString() == "PartialRefund") {
            offer.refundDeposit = policy.value("content").toObject().value("anti_spam_deposit_ratio").toDouble();
        }
        return offer;
    }
}

bool MakerOffer::available() const {
    return priceSatPerXmr > 0 && maxSat > 0 && maxSat >= minSat;
}

QString MakerOffer::host() const {
    // "/dns4/example.org/tcp/443/wss/p2p/..." or "/onion3/abc...xyz:9939/p2p/..."
    const QStringList parts = address.split('/', Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); i++) {
        const QString &protocol = parts[i];
        if (protocol == "dns4" || protocol == "dns6" || protocol == "dns" || protocol == "ip4" || protocol == "ip6") {
            return parts[i + 1];
        }
        if (protocol == "onion3") {
            const QString onion = parts[i + 1].section(':', 0, 0);
            return QString("%1….onion").arg(onion.left(8));
        }
    }
    return peerId.right(8);
}

Event parseLine(const QByteArray &line) {
    Event event;
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        return event;
    }

    const QJsonObject o = doc.object();
    const QString type = o.value("type").toString();

    if (type == "tor") {
        event.type = Event::Type::Tor;
        event.torStatus = o.value("status").toString();
    } else if (type == "started") {
        event.type = Event::Type::Started;
        event.usesTor = o.value("tor").toBool();
    } else if (type == "summary") {
        event.type = Event::Type::Summary;
        event.summary.makersKnown = o.value("makers_known").toInt();
        event.summary.dialing = o.value("dialing").toInt();
        event.summary.connected = o.value("connected").toInt();
        event.summary.quotesInflight = o.value("quotes_inflight").toInt();
        event.summary.offers = o.value("offers").toInt();
    } else if (type == "quotes") {
        event.type = Event::Type::Offers;
        for (const QJsonValue &value : o.value("quotes").toArray()) {
            event.offers.append(parseOffer(value.toObject()));
        }
    } else if (type == "error") {
        event.type = Event::Type::Error;
        event.message = o.value("message").toString();
    } else if (type == "stopped") {
        event.type = Event::Type::Stopped;
    } else if (type == "bitcoin") {
        event.type = Event::Type::Bitcoin;
        event.bitcoinStatus = o.value("status").toString();
        event.balanceSat = o.value("balance_sat").toInteger();
    } else if (type == "swap_started") {
        event.type = Event::Type::SwapStarted;
        event.swapId = o.value("swap_id").toString();
        event.btcAmountSat = o.value("btc_amount_sat").toInteger();
        event.lockFeeSat = o.value("lock_fee_sat").toInteger();
    } else if (type == "swap_resumed") {
        event.type = Event::Type::SwapResumed;
        event.swapId = o.value("swap_id").toString();
    } else if (type == "swap_state" || type == "swap_finished") {
        event.type = type == "swap_state" ? Event::Type::SwapState : Event::Type::SwapFinished;
        event.swapId = o.value("swap_id").toString();
        event.stage = o.value("stage").toString();
        event.stateText = o.value("state").toString();
    } else {
        event.type = Event::Type::Ignored;
    }
    return event;
}

QString discoveryHeadline(const DiscoverySummary &summary) {
    if (summary.quotesInflight > 0) return "Getting offers...";
    if (summary.dialing > 0) return "Dialing peers...";
    return "Waiting a few seconds...";
}

bool discoveryActive(const DiscoverySummary &summary) {
    return summary.quotesInflight > 0 || summary.dialing > 0;
}

std::optional<double> marketDeviation(quint64 priceSatPerXmr, double marketBtcPerXmr) {
    if (priceSatPerXmr == 0 || !(marketBtcPerXmr > 0)) {
        return std::nullopt;
    }
    const double offerBtcPerXmr = static_cast<double>(priceSatPerXmr) / 1e8;
    return (offerBtcPerXmr / marketBtcPerXmr - 1.0) * 100.0;
}

QString formatDeviation(double percent) {
    const double rounded = std::round(percent * 10.0) / 10.0;
    if (rounded == 0) return "0%";
    return QString("%1%2%").arg(rounded > 0 ? "+" : "").arg(QString::number(rounded, 'f', std::abs(rounded) >= 10 ? 0 : 1));
}

bool deviationNeedsWarning(double percent) {
    return percent >= 5.0 || percent <= -10.0;
}

QString formatBtc(quint64 sat) {
    return amount::fromAtomic(sat, 8);
}

QString formatDeposit(double ratio) {
    if (ratio <= 0) return "none";
    return QString("%1%").arg(QString::number(ratio * 100, 'g', 3));
}

}
