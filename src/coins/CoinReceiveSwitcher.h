// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINRECEIVESWITCHER_H
#define BISCUIT_COINRECEIVESWITCHER_H

#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class Wallet;

namespace biscuit::coins {

class CoinVault;
struct CoinParams;

// Receive tab of the unified wallet: a coin selector on top of Feather's
// Monero receive page and one page per Bitcoin-like coin.
class CoinReceiveSwitcher : public QWidget
{
    Q_OBJECT

public:
    CoinReceiveSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent = nullptr);

private:
    QWidget *coinPage(const CoinParams &params);
    void refresh();

    QPointer<CoinVault> m_vault;
    QComboBox *m_coin;
    QStackedWidget *m_pages;

    struct CoinWidgets {
        const CoinParams *params = nullptr;
        QStackedWidget *state = nullptr;   // 0 = not set up, 1 = address
        QLineEdit *address = nullptr;
        QLabel *qr = nullptr;
    };
    QList<CoinWidgets> m_coinWidgets;
};

}

#endif // BISCUIT_COINRECEIVESWITCHER_H
