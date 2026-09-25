// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPWIDGET_H
#define BISCUIT_SWAPWIDGET_H

#include <QPointer>
#include <QWidget>

#include "swap/SwapManager.h"

class InfoFrame;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class Wallet;

namespace Ui {
    class SwapWidget;
}

// Swap tab: XMR <-> other coins with an external address, in both directions.
// One side of a swap is always XMR from or to this wallet.
class SwapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SwapWidget(Wallet *wallet, QWidget *parent = nullptr);
    ~SwapWidget() override;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onFromChanged();
    void onToChanged();
    void onReverse();
    void onClear();
    void onGetOffers();
    void onCreateSwap();
    void onShowTradeDetails();
    void refreshTrades();

private:
    biscuit::swap::Asset xmr() const;
    static biscuit::swap::Asset assetOf(const QComboBox *combo);
    static void selectAsset(QComboBox *combo, const biscuit::swap::Asset &asset);
    bool sendsXmr() const;

    void loadAssets();
    // Tor mode: exchange swaps are off, atomic swaps remain.
    void updateTorMode();
    void updateForm();
    void clearOffers();
    void setBusy(bool busy, const QString &status = {});
    QString newWalletAddress(const QString &label);
    void createTrade(const biscuit::swap::Quote &quote);
    void showTrade(const QString &providerId, const QString &tradeId);
    void sendDeposit(const biscuit::swap::Trade &trade);

    QScopedPointer<Ui::SwapWidget> ui;
    QPointer<Wallet> m_wallet;
    biscuit::swap::SwapManager *m_manager;
    QList<biscuit::swap::Quote> m_quotes;
    bool m_updating = false;
    InfoFrame *m_torNotice = nullptr;
    QWidget *m_atomicTab = nullptr;
    bool m_mainnet = true;

    // Trade whose deposit is being sent from this wallet.
    QString m_pendingDepositProvider;
    QString m_pendingDepositTrade;
    QString m_pendingDepositDescription;
};

#endif // BISCUIT_SWAPWIDGET_H
