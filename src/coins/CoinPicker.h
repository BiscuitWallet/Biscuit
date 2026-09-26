// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINPICKER_H
#define BISCUIT_COINPICKER_H

#include <QIcon>
#include <QWidget>

class QButtonGroup;

namespace biscuit::coins {

// A row of buttons, one per coin, exactly one checked: every coin stays in
// sight, unlike a drop-down list.
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
    QButtonGroup *m_group;
};

// Monero, Bitcoin and Litecoin, in that order.
void addWalletCoins(CoinPicker *picker);

}

#endif // BISCUIT_COINPICKER_H
