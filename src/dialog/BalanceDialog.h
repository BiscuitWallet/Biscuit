// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_BALANCEDIALOG_H
#define FEATHER_BALANCEDIALOG_H

#include <QPointer>

#include "components.h"
#include "libwalletqt/Wallet.h"

class QGridLayout;
class QLabel;

namespace biscuit::coins {
    class CoinVault;
}

// Balance of every coin of the wallet: Monero, then each Bitcoin and
// Litecoin wallet, with their value and the total in the preferred currency.
class BalanceDialog : public WindowModalDialog
{
    Q_OBJECT

public:
    explicit BalanceDialog(QWidget *parent, Wallet *wallet);
    ~BalanceDialog() override;

private:
    void updateBalance();

    Wallet *m_wallet;
    QPointer<biscuit::coins::CoinVault> m_vault;
    QWidget *m_table;
    QLabel *m_note;
};

#endif //FEATHER_BALANCEDIALOG_H
