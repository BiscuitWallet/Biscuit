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

    void addCoin(const QIcon &icon, const QString &name);
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

// Monero, Bitcoin and Litecoin, in that order.
void addWalletCoins(CoinPicker *picker);
// Shows only the coins in this wallet (Monero always), which start at index
// `first` (1 after "All coins", shown only with two coins or more). With
// `addButton`, a "+" offers to add the others. Without it, a picker left
// with a single coin is hidden.
void showWalletCoins(CoinPicker *picker, CoinVault *vault, int first, bool addButton);

}

#endif // BISCUIT_COINPICKER_H
