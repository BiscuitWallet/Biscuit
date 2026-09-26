// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINPICKER_H
#define BISCUIT_COINPICKER_H

#include <QIcon>
#include <QWidget>

class QTabBar;

namespace biscuit::coins {

// The coins as a segmented control (same native look as the main tabs):
// every coin stays in sight, unlike a drop-down list.
class CoinPicker : public QWidget
{
    Q_OBJECT

public:
    explicit CoinPicker(QWidget *parent = nullptr);

    void addCoin(const QIcon &icon, const QString &name);
    int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void currentIndexChanged(int index);

private:
    QTabBar *m_tabs;
};

// Monero, Bitcoin and Litecoin, in that order.
void addWalletCoins(CoinPicker *picker);

}

#endif // BISCUIT_COINPICKER_H
