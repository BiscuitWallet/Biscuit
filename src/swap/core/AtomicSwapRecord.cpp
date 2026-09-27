// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapRecord.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace biscuit::swap::atomic {

bool isFinalStage(const QString &s) {
    return s == stage::done || s == stage::refunded || s == stage::punished || s == stage::cancelled;
}

bool fundsAtStake(const QString &s) {
    return !isFinalStage(s) && s != stage::setup;
}

QString stageText(const QString &s) {
    if (s == stage::setup) return "Agreeing with the maker";
    if (s == stage::btcLocked) return "BTC locked, waiting for the maker's XMR";
    if (s == stage::xmrLockSeen) return "Maker's XMR seen, waiting for confirmations";
    if (s == stage::xmrLocked) return "XMR locked, finishing the swap";
    if (s == stage::redeeming) return "Receiving the XMR";
    if (s == stage::done) return "Done";
    if (s == stage::refunding) return "Refunding the BTC";
    if (s == stage::refunded) return "BTC refunded";
    if (s == stage::punished) return "BTC kept by the maker";
    if (s == stage::cancelled) return "Cancelled, no BTC sent";
    return s;
}

namespace {
    // "0.01000000" -> "0.01"
    QString trimBtc(QString value) {
        if (value.contains('.')) {
            while (value.endsWith('0')) value.chop(1);
            if (value.endsWith('.')) value.chop(1);
        }
        return value;
    }
}

QString failureReason(const QString &error) {
    if (error.isEmpty()) {
        return {};
    }
    // Refusals sent by the maker (eigenwallet swap setup), most specific first.
    static const QRegularExpression below("minimum configured buy limit is ([0-9.]+) BTC");
    static const QRegularExpression above("maximum configured buy limit is ([0-9.]+) BTC");
    static const QRegularExpression rejected("Swap rejected: ([^\n]+)");
    if (const auto m = below.match(error); m.hasMatch()) {
        return QString("The maker refused: its real minimum is %1 BTC").arg(trimBtc(m.captured(1)));
    }
    if (const auto m = above.match(error); m.hasMatch()) {
        return QString("The maker refused: its maximum right now is %1 BTC").arg(trimBtc(m.captured(1)));
    }
    if (error.contains("XMR balance is currently too low")) {
        return "The maker does not have enough XMR right now";
    }
    if (error.contains("does not accept incoming swap requests")) {
        return "The maker is not accepting swaps right now";
    }
    if (error.contains("Seller encountered a problem")) {
        return "The maker could not give a live price right now";
    }
    if (const auto m = rejected.match(error); m.hasMatch()) {
        return QString("The maker rejected the swap: %1").arg(m.captured(1).trimmed());
    }
    // Otherwise the helper's own message (its first line).
    const QStringList lines = error.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? error.trimmed() : lines.first().trimmed();
}

quint64 AtomicSwapRecord::expectedXmrAtomic() const {
    if (priceSatPerXmr == 0) {
        return 0;
    }
    // btc / price XMR, in piconero: btcSat * 1e12 / priceSat.
    return static_cast<quint64>(static_cast<long double>(btcSat) * 1e12L / priceSatPerXmr);
}

QList<AtomicSwapRecord> recordsFromJson(const QByteArray &json) {
    QList<AtomicSwapRecord> records;
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = value.toObject();
        AtomicSwapRecord r;
        r.id = o.value("id").toString();
        if (r.id.isEmpty()) {
            continue;
        }
        r.walletId = o.value("wallet").toString();
        r.makerPeerId = o.value("maker_peer_id").toString();
        r.makerAddress = o.value("maker_address").toString();
        r.makerHost = o.value("maker_host").toString();
        r.priceSatPerXmr = o.value("price_sat_per_xmr").toInteger();
        r.btcSat = o.value("btc_sat").toInteger();
        r.lockFeeSat = o.value("lock_fee_sat").toInteger();
        r.xmrAddress = o.value("xmr_address").toString();
        r.tor = o.value("tor").toBool();
        r.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
        r.stage = o.value("stage").toString(stage::setup);
        r.stateText = o.value("state").toString();
        r.error = o.value("error").toString();
        records.append(r);
    }
    std::stable_sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.created > b.created; });
    return records;
}

QByteArray recordsToJson(const QList<AtomicSwapRecord> &records) {
    QJsonArray array;
    for (const AtomicSwapRecord &r : records) {
        array.append(QJsonObject{
            {"id", r.id},
            {"wallet", r.walletId},
            {"maker_peer_id", r.makerPeerId},
            {"maker_address", r.makerAddress},
            {"maker_host", r.makerHost},
            {"price_sat_per_xmr", static_cast<qint64>(r.priceSatPerXmr)},
            {"btc_sat", static_cast<qint64>(r.btcSat)},
            {"lock_fee_sat", static_cast<qint64>(r.lockFeeSat)},
            {"xmr_address", r.xmrAddress},
            {"tor", r.tor},
            {"created", r.created.toString(Qt::ISODate)},
            {"stage", r.stage},
            {"state", r.stateText},
            {"error", r.error},
        });
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

}
