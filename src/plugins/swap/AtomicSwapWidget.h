// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ATOMICSWAPWIDGET_H
#define BISCUIT_ATOMICSWAPWIDGET_H

#include <QWidget>

#include "swap/AtomicSwapDaemon.h"

namespace Ui {
    class AtomicSwapWidget;
}

// Atomic swap page of the Swap tab: BTC -> XMR with public makers (the
// eigenwallet network). Prototype: finds makers and lists their offers.
class AtomicSwapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AtomicSwapWidget(QWidget *parent = nullptr);
    ~AtomicSwapWidget() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void onDiscover();
    void onTorStatus(const QString &status);
    void onSummary(const biscuit::swap::atomic::DiscoverySummary &summary);
    void onOffers(const QList<biscuit::swap::atomic::MakerOffer> &offers);
    void onFailed(const QString &message);
    void onFinished();

    // With Tor enabled in Biscuit, nothing may leave outside Tor.
    void updateTorOption();
    void setBusy(bool busy);
    void updateDetails();

    QScopedPointer<Ui::AtomicSwapWidget> ui;
    biscuit::swap::AtomicSwapDaemon *m_daemon;
    biscuit::swap::atomic::DiscoverySummary m_summary;
    int m_unavailableOffers = 0;
    bool m_failed = false;
};

#endif // BISCUIT_ATOMICSWAPWIDGET_H
