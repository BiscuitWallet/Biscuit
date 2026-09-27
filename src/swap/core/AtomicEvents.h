// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_ATOMICEVENTS_H
#define BISCUIT_SWAP_ATOMICEVENTS_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>

// Events written by the biscuit-swapd helper (XMR/BTC atomic swaps), one JSON
// object per line on its stdout. Only what the UI needs is kept.
namespace biscuit::swap::atomic {

    // Counters for the "Dialing peers / Connected to N peers" bar.
    // Makers only: rendezvous points are not counted.
    struct DiscoverySummary {
        int makersKnown = 0;
        int dialing = 0;
        int connected = 0;
        int quotesInflight = 0;
        int offers = 0;

        bool operator==(const DiscoverySummary &o) const {
            return makersKnown == o.makersKnown && dialing == o.dialing && connected == o.connected
                && quotesInflight == o.quotesInflight && offers == o.offers;
        }
        bool operator!=(const DiscoverySummary &o) const { return !(*this == o); }
    };

    // What a maker offers: it sells XMR for BTC.
    struct MakerOffer {
        QString peerId;
        QString address;       // multiaddr the maker was reached at
        QString version;       // maker software version, may be empty
        quint64 priceSatPerXmr = 0;
        quint64 minSat = 0;
        quint64 maxSat = 0;
        // Share of the BTC the maker may keep if the swap is refunded
        // (anti-spam deposit), from 0 to 1. Display only.
        double refundDeposit = 0;

        // Makers without liquidity still answer, with a zero maximum or price.
        bool available() const;
        // Readable host: domain name, or a shortened onion address.
        QString host() const;
    };

    struct Event {
        enum class Type {
            Tor,        // torStatus: "bootstrapping" or "ready"
            Started,    // network stack running; usesTor
            Summary,
            Offers,
            Error,      // message; the helper exits after it
            Stopped,
            // Swap (buy / resume):
            Bitcoin,        // bitcoinStatus: "syncing" or "ready" (balanceSat)
            SwapStarted,    // swapId, btcAmountSat, lockFeeSat
            SwapResumed,    // swapId
            SwapState,      // swapId, stage, stateText
            SwapFinished,   // swapId, stage (a final stage)
            Ignored,    // per-peer events and unknown types
            Invalid,    // not a JSON object
        };

        Type type = Type::Invalid;
        QString torStatus;
        bool usesTor = false;
        DiscoverySummary summary;
        QList<MakerOffer> offers;
        QString message;

        QString bitcoinStatus;
        quint64 balanceSat = 0;
        QString swapId;
        QString stage;        // see AtomicSwapRecord.h
        QString stateText;    // the helper's own wording, for details
        quint64 btcAmountSat = 0;
        quint64 lockFeeSat = 0;
    };

    Event parseLine(const QByteArray &line);

    // Headline of the discovery bar, same wording as eigenwallet.
    QString discoveryHeadline(const DiscoverySummary &summary);
    // Something is in progress: the bar animates.
    bool discoveryActive(const DiscoverySummary &summary);

    // Price difference with the market, in percent: +25.6 means the offer
    // costs 25.6% more BTC than the market price. No value without a usable
    // reference price. Display only.
    std::optional<double> marketDeviation(quint64 priceSatPerXmr, double marketBtcPerXmr);
    // "+25.6%", "-1.2%", "0%".
    QString formatDeviation(double percent);
    // Far enough from the market to warn: overpriced, or suspiciously cheap
    // (often a stale price on the maker's side).
    bool deviationNeedsWarning(double percent);

    // "0.00852854" for 852854 sat.
    QString formatBtc(quint64 sat);
    // "1.5%" for 0.015, "none" for 0.
    QString formatDeposit(double ratio);
}

#endif // BISCUIT_SWAP_ATOMICEVENTS_H
