// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinPicker.h"

#include <QHBoxLayout>
#include <QTabBar>

#include "utils/Icons.h"

namespace biscuit::coins {

CoinPicker::CoinPicker(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabBar(this))
{
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setIconSize(QSize(16, 16));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    layout->addWidget(m_tabs);
    layout->addStretch();
    connect(m_tabs, &QTabBar::currentChanged, this, &CoinPicker::currentIndexChanged);
}

void CoinPicker::addCoin(const QIcon &icon, const QString &name) {
    m_tabs->addTab(icon, name);
}

int CoinPicker::currentIndex() const {
    return m_tabs->currentIndex();
}

void CoinPicker::setCurrentIndex(int index) {
    m_tabs->setCurrentIndex(index);
}

void addWalletCoins(CoinPicker *picker) {
    picker->addCoin(QIcon(":/assets/images/appicons/monero.png"), "Monero");
    picker->addCoin(icons()->icon("bitcoin.png"), "Bitcoin");
    picker->addCoin(icons()->icon("litecoin.png"), "Litecoin");
}

}
