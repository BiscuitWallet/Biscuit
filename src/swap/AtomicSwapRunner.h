// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ATOMICSWAPRUNNER_H
#define BISCUIT_ATOMICSWAPRUNNER_H

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include "swap/core/AtomicEvents.h"
#include "swap/core/AtomicSwapRecord.h"

class Wallet;

namespace biscuit::swap {

// BTC -> XMR atomic swaps of one open wallet. Starts biscuit-swapd for a new
// swap, relaunches it for unfinished ones (at startup, after an error), and
// keeps the list of swaps in the wallet cache.
//
// The BTC is spent from the selected Bitcoin wallet of Biscuit (the helper
// gets its seed on stdin) and the XMR arrives on a new subaddress. One swap
// runs at a time: the helper's database and Tor state are shared.
class AtomicSwapRunner : public QObject
{
    Q_OBJECT

public:
    explicit AtomicSwapRunner(Wallet *wallet, QObject *parent = nullptr);
    ~AtomicSwapRunner() override;

    QList<atomic::AtomicSwapRecord> records() const { return m_records; }
    // A swap is running or waiting to be resumed: no new one can start.
    bool busy() const;
    QString runningSwapId() const { return m_runningId; }

    // Starts a swap with `offer` for `btcSat` from the selected Bitcoin
    // wallet. tor = false shows the IP address to the maker: only after the
    // user agreed. Returns false (and why) if it could not start.
    bool start(const atomic::MakerOffer &offer, quint64 btcSat, bool tor, QString *error);

    // Relaunches the next unfinished swap, if any (vault unlocked).
    void resumeNext();

signals:
    void recordsChanged();
    // Progress text of the running swap ("Syncing the Bitcoin wallet"...).
    void activity(const QString &swapId, const QString &text);

private:
    bool launch(const atomic::AtomicSwapRecord &record, bool resume, QString *error);
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void update(const QString &id, const std::function<void(atomic::AtomicSwapRecord &)> &change);
    void save();
    // Subaddress receiving the XMR: an unused one left by a cancelled swap,
    // or a new one.
    QString swapSubaddress(const QString &label);

    QPointer<Wallet> m_wallet;
    QList<atomic::AtomicSwapRecord> m_records;
    QPointer<QProcess> m_process;
    QString m_runningId;
    QString m_lastError;
    QTimer m_retryTimer;
};

}

#endif // BISCUIT_ATOMICSWAPRUNNER_H
