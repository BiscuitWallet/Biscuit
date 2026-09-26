// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINWALLETBAR_H
#define BISCUIT_COINWALLETBAR_H

#include <QPointer>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;

namespace biscuit::coins {

class CoinVault;
struct CoinParams;

// The Bitcoin (or Litecoin) wallets of this wallet as buttons, the selected
// one checked, and "Add wallet" to add another one from its seed or a new seed.
// Right-click on an added wallet to remove it. Hidden while locked.
class CoinWalletBar : public QWidget
{
    Q_OBJECT

public:
    CoinWalletBar(CoinVault *vault, const CoinParams &params, QWidget *parent = nullptr);

    // Shown only when active and Bitcoin/Litecoin are unlocked. Screens with
    // one bar per coin activate the one of the coin in use.
    void setActive(bool active);
    // Label before the buttons ("Wallet:" by default, empty for none).
    void setTitle(const QString &title);

private:
    void rebuild();
    void updateBalances();
    QString format(quint64 amount) const;
    void confirmRemove(const QString &id, const QString &name);

    QPointer<CoinVault> m_vault;
    const CoinParams &m_params;
    QHBoxLayout *m_layout;
    QButtonGroup *m_group;
    QPointer<QLabel> m_balance;
    bool m_active = true;
    QString m_title = "Wallet:";
};

}

#endif // BISCUIT_COINWALLETBAR_H
