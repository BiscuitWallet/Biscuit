// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinPicker.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QStyleOptionTabBarBase>
#include <QStylePainter>
#include <QTabBar>
#include <QToolButton>

#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "utils/Icons.h"
#include "widgets/PixelIcons.h"

namespace biscuit::coins {

CoinPicker::CoinPicker(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabBar(this))
    , m_add(new QToolButton(this))
{
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    // Every coin in sight: never scroll arrows, the bar is as wide as its tabs.
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setIconSize(QSize(16, 16));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    layout->addWidget(m_tabs);
    m_add->setText("+");
    m_add->setAutoRaise(true);
    m_add->setToolTip("Add a coin to this wallet");
    m_add->hide();
    layout->addWidget(m_add);
    layout->addStretch();
    connect(m_tabs, &QTabBar::currentChanged, this, &CoinPicker::currentIndexChanged);
    connect(m_add, &QToolButton::clicked, this, &CoinPicker::addRequested);
}

void CoinPicker::setCoinVisible(int index, bool visible) {
    if (m_tabs->isTabVisible(index) == visible) {
        return;
    }
    m_tabs->setTabVisible(index, visible);
    // A coin added later: the bar grows to show it (it kept its old width).
    m_tabs->updateGeometry();
    m_tabs->adjustSize();
    update();   // the base line follows the tabs
}

bool CoinPicker::isCoinVisible(int index) const {
    return m_tabs->isTabVisible(index);
}

int CoinPicker::visibleCount() const {
    int count = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->isTabVisible(i)) ++count;
    }
    return count;
}

void CoinPicker::setAddVisible(bool visible) {
    m_add->setVisible(visible);
}

void CoinPicker::addCoin(const QIcon &icon, const QString &name, const QString &ticker) {
    m_tabs->setTabData(m_tabs->addTab(icon, name), ticker);
}

QString CoinPicker::ticker(int index) const {
    return m_tabs->tabData(index).toString();
}

int CoinPicker::indexOf(const QString &ticker) const {
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (this->ticker(i) == ticker) return i;
    }
    return -1;
}

int CoinPicker::count() const {
    return m_tabs->count();
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

void addWalletCoins(CoinPicker *picker, bool ethereum) {
    picker->addCoin(PixelIcons::icon("monero"), "Monero", "XMR");
    picker->addCoin(icons()->icon("bitcoin.png"), "Bitcoin", bitcoin().ticker);
    picker->addCoin(icons()->icon("litecoin.png"), "Litecoin", litecoin().ticker);
    if (ethereum) {
        // The tokens on Ethereum get their own tab: they are what people send.
        picker->addCoin(PixelIcons::icon("ethereum"), "Ethereum", coins::ethereum().ticker);
        picker->addCoin(PixelIcons::icon("tether"), "USDT", "USDT");
        picker->addCoin(PixelIcons::icon("usdc"), "USDC", "USDC");
    }
}

QStringList addableTickers() {
    QStringList list;
    for (const CoinParams *params : walletCoins()) list << params->ticker;
    for (const eth::Token &token : eth::tokens()) list << token.symbol;
    return list;
}

void showWalletCoins(CoinPicker *picker, CoinVault *vault, bool addButton) {
    const QPointer<CoinVault> guard(vault);
    auto update = [picker, guard, addButton] {
        if (!guard) {
            return;
        }
        const int monero = picker->indexOf("XMR");
        const int all = picker->indexOf("*");
        bool missing = false;
        int coins = 0;
        for (int i = 0; i < picker->count(); ++i) {
            const QString ticker = picker->ticker(i);
            if (!coinOfTicker(ticker)) {
                coins += i == monero;
                continue;
            }
            // While locked, which coins the wallet holds is not known: shown.
            const bool held = guard->assetState(ticker) != CoinVault::CoinState::NotAdded;
            missing = missing || !held;
            coins += held;
            if (!held && picker->currentIndex() == i) {
                picker->setCurrentIndex(monero);
            }
            picker->setCoinVisible(i, held);
        }
        if (all >= 0) {
            // "All coins" only means something with two coins or more.
            if (coins < 2 && picker->currentIndex() == all) {
                picker->setCurrentIndex(monero);
            }
            picker->setCoinVisible(all, coins >= 2);
        }
        // Any coin or token not added, even one without a page in this tab.
        for (const QString &ticker : addableTickers()) {
            missing = missing || guard->assetState(ticker) == CoinVault::CoinState::NotAdded;
        }
        picker->setAddVisible(addButton && missing);
        picker->setVisible(addButton || coins > 1);
    };
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged}) {
        QObject::connect(vault, signal, picker, update);
    }
    update();

    if (!addButton) {
        return;
    }
    QObject::connect(picker, &CoinPicker::addRequested, picker, [picker, guard] {
        if (!guard) {
            return;
        }
        // Every coin not added yet, even one this tab has no page for: it is
        // added to the wallet all the same, and shown here if it has a page.
        QMenu menu(picker);
        for (const QString &ticker : addableTickers()) {
            if (guard->assetState(ticker) != CoinVault::CoinState::NotAdded) {
                continue;
            }
            menu.addAction(QString("Add %1…").arg(assetLabel(ticker)), picker, [picker, guard, ticker] {
                if (guard && addAssetToWallet(guard, ticker, picker->window())) {
                    const int index = picker->indexOf(ticker);
                    if (index >= 0) picker->setCurrentIndex(index);
                }
            });
        }
        menu.exec(QCursor::pos());
    });
}

}
