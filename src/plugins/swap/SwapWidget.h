// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPWIDGET_H
#define BISCUIT_SWAPWIDGET_H

#include <QPointer>
#include <QWidget>

#include "swap/SwapManager.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class Wallet;

// Swap tab: XMR <-> other coins with an external address, in both directions.
class SwapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SwapWidget(Wallet *wallet, QWidget *parent = nullptr);

private slots:
    void onDirectionChanged();
    void onGetOffers();
    void onCreateSwap();
    void onTradeActivated();
    void refreshTrades();

private:
    enum Direction {
        SendXmr = 0,     // XMR from this wallet -> coin to an external address
        ReceiveXmr       // coin from outside -> XMR to this wallet
    };

    Direction direction() const;
    biscuit::swap::Asset selectedCoin() const;
    biscuit::swap::Asset xmr() const;
    void loadAssets();
    void setBusy(bool busy, const QString &status = {});
    QString newWalletAddress(const QString &label);
    void createTrade(const biscuit::swap::Quote &quote);
    void showTrade(const QString &providerId, const QString &tradeId);
    void sendDeposit(const biscuit::swap::Trade &trade);

    QPointer<Wallet> m_wallet;
    biscuit::swap::SwapManager *m_manager;
    QList<biscuit::swap::Quote> m_quotes;

    QComboBox *m_direction = nullptr;
    QComboBox *m_coin = nullptr;
    QLineEdit *m_amount = nullptr;
    QLabel *m_amountUnit = nullptr;
    QLabel *m_payoutLabel = nullptr;
    QLineEdit *m_payout = nullptr;
    QLabel *m_refundLabel = nullptr;
    QLineEdit *m_refund = nullptr;
    QComboBox *m_rateType = nullptr;
    QComboBox *m_minKyc = nullptr;
    QPushButton *m_btnOffers = nullptr;
    QTreeWidget *m_offers = nullptr;
    QPushButton *m_btnCreate = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_trades = nullptr;

    // Trade whose deposit is being sent from this wallet.
    QString m_pendingDepositProvider;
    QString m_pendingDepositTrade;
    QString m_pendingDepositDescription;
};

#endif // BISCUIT_SWAPWIDGET_H
