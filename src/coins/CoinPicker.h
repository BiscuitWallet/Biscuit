// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINPICKER_H
#define BISCUIT_COINPICKER_H

#include <QIcon>
#include <QWidget>

class QTabBar;
class QToolButton;

namespace biscuit::coins {

// The coins as a segmented control (same native look as the main tabs):
// every coin stays in sight, unlike a drop-down list. A line runs under it
// across the whole width, like under the main tabs.
class CoinPicker : public QWidget
{
    Q_OBJECT

public:
    explicit CoinPicker(QWidget *parent = nullptr);

    // `ticker`: "XMR", "BTC"…, or "*" for "All coins".
    void addCoin(const QIcon &icon, const QString &name, const QString &ticker);
    QString ticker(int index) const;
    int indexOf(const QString &ticker) const;
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    // Hidden coins keep their index (pages are found by index).
    void setCoinVisible(int index, bool visible);
    bool isCoinVisible(int index) const;
    int visibleCount() const;
    // A "+" after the coins.
    void setAddVisible(bool visible);

signals:
    void currentIndexChanged(int index);
    void addRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTabBar *m_tabs;
    QToolButton *m_add;
};

class CoinVault;

// What "+" can add: BTC, LTC, ETH, USDT, USDC.
QStringList addableTickers();
// Monero, Bitcoin, Litecoin and, where the page handles it, Ethereum and its
// tokens (`tokens` false: Ethereum alone, e.g. for addresses, the same for all).
void addWalletCoins(CoinPicker *picker, bool ethereum = false, bool tokens = true);
// Shows only the coins in this wallet (Monero always), and "All coins" only
// with two coins or more. With `addButton`, a "+" offers to add the coins
// this picker has a page for. Without it, a picker left with a single coin
// is hidden.
void showWalletCoins(CoinPicker *picker, CoinVault *vault, bool addButton);

}

#endif // BISCUIT_COINPICKER_H
