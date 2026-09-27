// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_ATOMICSWAPRECORD_H
#define BISCUIT_SWAP_ATOMICSWAPRECORD_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// The user's BTC -> XMR atomic swaps, as Biscuit remembers them (in the
// wallet cache). The swap itself (keys, transactions) lives in the helper's
// database; this is what the UI shows and what is needed to resume it.
namespace biscuit::swap::atomic {

    // Stages reported by biscuit-swapd, in order:
    //   setup -> btc_locked -> xmr_lock_seen -> xmr_locked -> redeeming -> done
    // or, when the swap is aborted after the BTC was locked:
    //   refunding -> refunded (or punished).
    // "cancelled": stopped during setup, no BTC ever left the wallet (Biscuit's own).
    namespace stage {
        inline const QString setup = "setup";
        inline const QString btcLocked = "btc_locked";
        inline const QString xmrLockSeen = "xmr_lock_seen";
        inline const QString xmrLocked = "xmr_locked";
        inline const QString redeeming = "redeeming";
        inline const QString done = "done";
        inline const QString refunding = "refunding";
        inline const QString refunded = "refunded";
        inline const QString punished = "punished";
        inline const QString cancelled = "cancelled";
    }

    // Nothing left to do for this swap.
    bool isFinalStage(const QString &stage);
    // BTC may be locked: the helper must run until the swap ends, or the
    // automatic refund cannot happen.
    bool fundsAtStake(const QString &stage);
    // Short status for the list, e.g. "Waiting for the maker's XMR".
    QString stageText(const QString &stage);
    // Why a swap stopped, in plain words, from the helper's error: the
    // maker's own reason when it refused ("its real minimum is 0.01 BTC").
    // Empty for an empty error.
    QString failureReason(const QString &error);

    struct AtomicSwapRecord {
        QString id;              // swap ID (UUID) from the helper
        QString walletId;        // Bitcoin wallet it spends from (CoinVault entry)
        QString makerPeerId;
        QString makerAddress;    // multiaddr
        QString makerHost;       // readable name
        quint64 priceSatPerXmr = 0;
        quint64 btcSat = 0;
        quint64 lockFeeSat = 0;
        QString xmrAddress;      // Biscuit subaddress receiving the XMR
        bool tor = false;
        QDateTime created;
        QString stage = stage::setup;
        QString stateText;
        QString error;           // last error from the helper, if any

        // Expected XMR (atomic units, 1e-12) at the agreed price.
        quint64 expectedXmrAtomic() const;
    };

    // Newest first.
    QList<AtomicSwapRecord> recordsFromJson(const QByteArray &json);
    QByteArray recordsToJson(const QList<AtomicSwapRecord> &records);

    inline constexpr const char *walletAttribute = "biscuit.atomicswaps";
}

#endif // BISCUIT_SWAP_ATOMICSWAPRECORD_H
