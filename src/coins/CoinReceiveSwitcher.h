// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINRECEIVESWITCHER_H
#define BISCUIT_COINRECEIVESWITCHER_H

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class Wallet;

namespace biscuit::coins {

class CoinPicker;
class CoinWallet;
class CoinVault;
struct CoinParams;

// Receive tab of the unified wallet: coin buttons on top of Feather's
// Monero receive page and one page per Bitcoin-like coin.
class CoinReceiveSwitcher : public QWidget
{
    Q_OBJECT

public:
    CoinReceiveSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent = nullptr);

private:
    QWidget *coinPage(const CoinParams &params);
    void refresh();
    CoinWallet *coin(const CoinParams &params) const;

    QPointer<CoinVault> m_vault;
    CoinPicker *m_coin;
    QStackedWidget *m_pages;

    struct CoinWidgets {
        const CoinParams *params = nullptr;
        QStackedWidget *state = nullptr;   // 0 = not added or locked, 1 = address
        QLabel *setupText = nullptr;
        QPushButton *setupButton = nullptr;
        QLineEdit *address = nullptr;
        QLabel *qr = nullptr;
        QCheckBox *keep = nullptr;
        QLabel *hint = nullptr;
    };
    QList<CoinWidgets> m_coinWidgets;
};

}

#endif // BISCUIT_COINRECEIVESWITCHER_H
