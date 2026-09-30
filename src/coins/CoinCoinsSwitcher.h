// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINCOINSSWITCHER_H
#define BISCUIT_COINCOINSSWITCHER_H

#include <QPointer>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTreeWidget;
class Wallet;

namespace biscuit::coins {

class CoinPicker;
class CoinVault;
struct CoinParams;

// The Coins tab with a Monero / Bitcoin / Litecoin picker: Feather's
// Monero coin control, and coin control for the selected Bitcoin or
// Litecoin wallet (freeze coins, send only chosen coins).
class CoinCoinsSwitcher : public QWidget
{
    Q_OBJECT

public:
    CoinCoinsSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent = nullptr);

signals:
    // "Send these coins": the Send tab should open on this coin.
    void sendCoins(int coinIndex);

private:
    QTreeWidget *makeTree(const CoinParams &params);
    void refresh();
    void fillTree(QTreeWidget *tree, QLabel *summary, const CoinParams &params);
    QStringList selectedKeys(QTreeWidget *tree) const;

    QPointer<Wallet> m_wallet;
    QPointer<CoinVault> m_vault;
    CoinPicker *m_picker;
    QStackedWidget *m_pages;
    QTreeWidget *m_btc;
    QTreeWidget *m_ltc;
    QLabel *m_btcSummary;
    QLabel *m_ltcSummary;
};

}

#endif // BISCUIT_COINCOINSSWITCHER_H
