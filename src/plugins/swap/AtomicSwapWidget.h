// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ATOMICSWAPWIDGET_H
#define BISCUIT_ATOMICSWAPWIDGET_H

#include <QWidget>

class QLabel;
class RetroBusyBar;
class QTimer;
class Wallet;

#include "swap/AtomicSwapDaemon.h"
#include "swap/AtomicSwapRunner.h"

namespace Ui {
    class AtomicSwapWidget;
}

// Atomic swap page of the Swap tab: BTC -> XMR with public makers (the
// eigenwallet network). Finds makers, lists their offers, starts a swap with
// the chosen one and follows the user's swaps.
class AtomicSwapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AtomicSwapWidget(Wallet *wallet, QWidget *parent = nullptr);
    ~AtomicSwapWidget() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void onDiscover();
    void onTorStatus(const QString &status);
    void onSummary(const biscuit::swap::atomic::DiscoverySummary &summary);
    void onOffers(const QList<biscuit::swap::atomic::MakerOffer> &offers);
    void showOffers();
    void onFailed(const QString &message);
    void onFinished();
    void onSwap();
    void refreshSwaps();
    void onClearSwaps();
    void updateSwapButton();

    // With Tor enabled in Biscuit, nothing may leave outside Tor.
    void updateTorOption();
    void setBusy(bool busy);

    // Dialing indicator: a handset and "Dialing makers..." with dots that
    // appear one by one, like a modem; the handset picks up once a maker
    // answers, and a check mark shows when offers are in.
    enum class Phone { Hidden, Waiting, Dialing, PickedUp };
    int m_modemPhase = 0;
    void setHeadline(const QString &text, bool dialing);
    void setPhone(Phone phone);
    QLabel *m_modem = nullptr;
    RetroBusyBar *m_busyBar = nullptr;
    QTimer *m_dialTimer = nullptr;
    QString m_headlineBase;
    int m_dots = 0;
    Phone m_phoneState = Phone::Hidden;
    void updateDetails();

    QScopedPointer<Ui::AtomicSwapWidget> ui;
    Wallet *m_wallet;
    biscuit::swap::AtomicSwapDaemon *m_daemon;
    biscuit::swap::AtomicSwapRunner *m_runner;
    QList<biscuit::swap::atomic::MakerOffer> m_shownOffers;   // rows of tree_offers
    biscuit::swap::atomic::DiscoverySummary m_summary;
    QList<biscuit::swap::atomic::MakerOffer> m_offers;
    int m_unavailableOffers = 0;
    bool m_failed = false;
};

#endif // BISCUIT_ATOMICSWAPWIDGET_H
