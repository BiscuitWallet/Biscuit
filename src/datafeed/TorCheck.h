// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_TORCHECK_H
#define BISCUIT_TORCHECK_H

#include <QObject>
#include <QTimer>

namespace biscuit::datafeed {

// In Tor mode, asks biscuitwallet.com from time to time whether Biscuit's own
// traffic reaches it through Tor, so a Tor mode that silently is not one (a
// wrong SOCKS proxy, something else listening on 127.0.0.1:9050) shows in the
// status bar. The site answers only yes, no or unknown: it never sends back
// the address and keeps nothing. Off when third-party data is disabled.
class TorCheck : public QObject {
    Q_OBJECT

public:
    enum class Result { Unknown, Tor, NotTor };

    static TorCheck *instance();
    Result result() const { return m_result; }

    // The proxy or the Tor connection changed: forget the answer, ask soon.
    void restart();

signals:
    void resultChanged(biscuit::datafeed::TorCheck::Result result);

private:
    explicit TorCheck(QObject *parent);
    static bool enabled();
    void check();
    void setResult(Result result);
    void schedule(int ms);

    QTimer m_timer;
    Result m_result = Result::Unknown;
    quint64 m_generation = 0;
    bool m_torConnected = false;
};

}

#endif // BISCUIT_TORCHECK_H
