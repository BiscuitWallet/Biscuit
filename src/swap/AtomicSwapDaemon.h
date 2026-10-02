// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ATOMICSWAPDAEMON_H
#define BISCUIT_ATOMICSWAPDAEMON_H

#include <QObject>
#include <QPointer>
#include <QProcess>

#include "swap/core/AtomicEvents.h"

namespace biscuit::swap {

// Runs the biscuit-swapd helper to discover makers (XMR/BTC atomic swaps) and
// turns its stdout into signals. Swaps themselves: AtomicSwapRunner.
class AtomicSwapDaemon : public QObject
{
    Q_OBJECT

public:
    explicit AtomicSwapDaemon(QObject *parent = nullptr);
    ~AtomicSwapDaemon() override;

    // Empty when the helper cannot be found. BISCUIT_SWAPD overrides the path
    // (development builds); otherwise it is expected next to the Biscuit binary.
    static QString helperPath();
    // Tails or Whonix, whose system sends everything through Tor ("" otherwise).
    static QString systemTorName();
    // The helper's network flag. On Tails and Whonix the system already sends
    // everything through Tor (or blocks it), so the helper connects directly
    // instead of running its own Tor inside the system's one.
    static QString networkFlag(bool tor);

    bool isRunning() const;
    // tor = false reveals the IP address to rendezvous points and makers:
    // only call it after the user agreed.
    void start(bool tor);
    void stop();

signals:
    void torStatusChanged(const QString &status);
    void discoveryStarted(bool usesTor);
    void summaryChanged(const biscuit::swap::atomic::DiscoverySummary &summary);
    void offersChanged(const QList<biscuit::swap::atomic::MakerOffer> &offers);
    void failed(const QString &message);
    void finished();

private:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QPointer<QProcess> m_process;
    bool m_reportedError = false;
    bool m_stopping = false;
};

}

#endif // BISCUIT_ATOMICSWAPDAEMON_H
