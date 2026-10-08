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
    m_tabs->setTabVisible(index, visible);
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

void showWalletCoins(CoinPicker *picker, CoinVault *vault, int first, bool addButton) {
    const QPointer<CoinVault> guard(vault);
    auto update = [picker, guard, first, addButton] {
        if (!guard) {
            return;
        }
        const auto &coins = walletCoins();
        bool missing = false;
        for (int i = 0; i < coins.size(); ++i) {
            const int index = first + 1 + i;
            // While locked, which coins the wallet holds is not known: shown.
            const bool held = guard->coinState(*coins.at(i)) != CoinVault::CoinState::NotAdded;
            missing = missing || !held;
            if (!held && picker->currentIndex() == index) {
                picker->setCurrentIndex(first);   // Monero
            }
            picker->setCoinVisible(index, held);
        }
        if (first > 0) {
            // "All coins" only means something with two coins or more.
            const bool all = picker->visibleCount() - (picker->isCoinVisible(0) ? 1 : 0) > 1;
            if (!all && picker->currentIndex() == 0) {
                picker->setCurrentIndex(first);
            }
            picker->setCoinVisible(0, all);
        }
        picker->setAddVisible(addButton && missing);
        picker->setVisible(addButton || picker->visibleCount() > 1);
    };
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged}) {
        QObject::connect(vault, signal, picker, update);
    }
    update();

    if (!addButton) {
        return;
    }
    QObject::connect(picker, &CoinPicker::addRequested, picker, [picker, guard, first] {
        if (!guard) {
            return;
        }
        QMenu menu(picker);
        const auto &coins = walletCoins();
        for (int i = 0; i < coins.size(); ++i) {
            const CoinParams *params = coins.at(i);
            if (guard->coinState(*params) != CoinVault::CoinState::NotAdded) {
                continue;
            }
            menu.addAction(QString("Add %1…").arg(params->name), picker, [picker, guard, params, index = first + 1 + i] {
                if (guard && addCoinToWallet(guard, *params, picker->window())) {
                    picker->setCurrentIndex(index);
                }
            });
        }
        menu.exec(QCursor::pos());
    });
}

}
