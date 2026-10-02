// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinPicker.h"

#include <QHBoxLayout>
#include <QStyleOptionTabBarBase>
#include <QStylePainter>
#include <QTabBar>

#include "utils/Icons.h"
#include "widgets/PixelIcons.h"

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

void CoinPicker::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    // The tab bar only spans the coins: draw its base line across the whole
    // width, where the selected tab overlaps it (the tabs paint on top).
    // Colours from the tab bar itself: it always follows the current light or
    // dark palette, while this container could keep the previous one.
    QStyleOptionTabBarBase base;
    base.initFrom(m_tabs);
    base.shape = m_tabs->shape();
    const int overlap = style()->pixelMetric(QStyle::PM_TabBarBaseOverlap, nullptr, m_tabs);
    base.rect = QRect(0, m_tabs->geometry().bottom() - overlap + 1, width(), overlap);
    base.tabBarRect = m_tabs->geometry();
    base.selectedTabRect = m_tabs->tabRect(m_tabs->currentIndex()).translated(m_tabs->pos());
    QStylePainter(this).drawPrimitive(QStyle::PE_FrameTabBarBase, base);
}

void addWalletCoins(CoinPicker *picker) {
    picker->addCoin(PixelIcons::icon("monero"), "Monero");
    picker->addCoin(icons()->icon("bitcoin.png"), "Bitcoin");
    picker->addCoin(icons()->icon("litecoin.png"), "Litecoin");
}

}
