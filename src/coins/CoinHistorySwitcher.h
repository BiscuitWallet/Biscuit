// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINHISTORYSWITCHER_H
#define BISCUIT_COINHISTORYSWITCHER_H

#include <QPointer>
#include <QWidget>

class QStackedWidget;
class QTreeWidget;
class Wallet;

namespace biscuit::coins {

class CoinPicker;

class CoinVault;
struct CoinParams;

// History tab of the unified wallet: every coin in one list by default, or a
// single coin (the Monero page is Feather's history widget).
class CoinHistorySwitcher : public QWidget
{
    Q_OBJECT

public:
    CoinHistorySwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent = nullptr);

private:
    QTreeWidget *makeTree(bool withCoinColumn);
    void refresh();
    void fillCoinTree(QTreeWidget *tree, const CoinParams &params);

    QPointer<Wallet> m_wallet;
    QPointer<CoinVault> m_vault;
    CoinPicker *m_filter;
    QStackedWidget *m_pages;
    QTreeWidget *m_all;
    QTreeWidget *m_btc;
    QTreeWidget *m_ltc;
};

}

#endif // BISCUIT_COINHISTORYSWITCHER_H
