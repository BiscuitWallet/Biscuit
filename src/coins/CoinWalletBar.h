// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINWALLETBAR_H
#define BISCUIT_COINWALLETBAR_H

#include <QPointer>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;

namespace biscuit::coins {

class CoinVault;
struct CoinParams;

// The Bitcoin (or Litecoin) wallets of this wallet as buttons, the selected
// one checked, and "+ Add" to add another one from its seed or a new seed.
// Right-click on an added wallet to remove it. Hidden while locked.
class CoinWalletBar : public QWidget
{
    Q_OBJECT

public:
    CoinWalletBar(CoinVault *vault, const CoinParams &params, QWidget *parent = nullptr);

private:
    void rebuild();
    void confirmRemove(const QString &id, const QString &name);

    QPointer<CoinVault> m_vault;
    const CoinParams &m_params;
    QHBoxLayout *m_layout;
    QButtonGroup *m_group;
};

}

#endif // BISCUIT_COINWALLETBAR_H
