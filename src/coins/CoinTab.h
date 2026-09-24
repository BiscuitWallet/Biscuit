// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINTAB_H
#define BISCUIT_COINTAB_H

#include <QPointer>
#include <QWidget>

#include "CoinParams.h"

class Wallet;

namespace Ui {
    class CoinTab;
}

namespace biscuit::coins {

class CoinVault;
class CoinWallet;

// "Bitcoin" or "Litecoin" tab: setup/unlock, then History, Send and Receive.
class CoinTab : public QWidget
{
    Q_OBJECT

public:
    CoinTab(Wallet *wallet, const CoinParams &params, QWidget *parent = nullptr);
    ~CoinTab() override;

private:
    void updatePage();
    void bindWallet();
    void refresh();
    void refreshFees();
    void unlock();
    void send();
    void clearSend();
    QString formatAmount(qint64 sats, bool sign = false) const;

    QScopedPointer<Ui::CoinTab> ui;
    const CoinParams &m_params;
    QPointer<CoinVault> m_vault;
    QPointer<CoinWallet> m_coin;
    bool m_sendAll = false;
};

}

#endif // BISCUIT_COINTAB_H
