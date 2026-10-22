// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinCoinsSwitcher.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Addresses.h"
#include "Amount.h"
#include "CoinPicker.h"
#include "CoinSendController.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "CoinWalletBar.h"
#include "libwalletqt/Wallet.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Column { ColAmount = 0, ColAddress, ColStatus, ColCoin };
    constexpr int KeyRole = Qt::UserRole;
    constexpr int SortRole = Qt::UserRole + 1;

    // Amounts sort by value, not as text.
    class CoinItem : public QTreeWidgetItem {
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
        bool operator<(const QTreeWidgetItem &other) const override {
            const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
            if (column == ColAmount) {
                return data(ColAmount, SortRole).toULongLong() < other.data(ColAmount, SortRole).toULongLong();
            }
            return QTreeWidgetItem::operator<(other);
        }
    };
}

CoinCoinsSwitcher::CoinCoinsSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent)
    : QWidget(parent)
    , m_wallet(wallet)
    , m_vault(CoinVault::forWallet(wallet))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_picker = new CoinPicker(this);
    addWalletCoins(m_picker);
    layout->addWidget(m_picker);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(moneroPage);
    // Bitcoin, Litecoin, Dogecoin: one page each, in the picker's order.
    for (const CoinParams *params : electrumCoins()) {
        auto *tree = makeTree(*params);
        auto *summary = new QLabel(this);
        m_trees.insert(params->ticker, tree);
        m_summaries.insert(params->ticker, summary);
        auto *page = new QWidget(m_pages);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new CoinWalletBar(m_vault, *params, page));
        pageLayout->addWidget(tree);
        auto *bottom = new QHBoxLayout;
        summary->setParent(page);
        summary->setWordWrap(true);
        bottom->addWidget(summary, 1);
        // Many small coins: one, at the slow fee (the selected ones, or all).
        auto *consolidate = new QPushButton("Consolidate…", page);
        consolidate->setAutoDefault(false);
        consolidate->setToolTip("Combine small coins into one when fees are low, so that later payments cost less. "
                                "Select coins first to combine only those.");
        bottom->addWidget(consolidate, 0, Qt::AlignTop);
        pageLayout->addLayout(bottom);
        m_pages->addWidget(page);
        connect(consolidate, &QPushButton::clicked, this, [this, tree, params] {
            CoinSendController(m_wallet).consolidate(this, *params, selectedKeys(tree));
        });
    }
    layout->addWidget(m_pages);

    connect(m_picker, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    showWalletCoins(m_picker, m_vault, false);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged,
                        &CoinVault::walletUpdated, &CoinVault::coinSelectionChanged}) {
        connect(m_vault, signal, this, &CoinCoinsSwitcher::refresh);
    }
    refresh();
}

QTreeWidget *CoinCoinsSwitcher::makeTree(const CoinParams &params) {
    auto *tree = new QTreeWidget(this);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setHeaderLabels({"Amount", "Address", "Status", "Coin"});
    tree->header()->setSectionResizeMode(ColAddress, QHeaderView::Stretch);
    tree->header()->setStretchLastSection(false);
    tree->setSortingEnabled(true);
    tree->sortByColumn(ColAmount, Qt::DescendingOrder);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);

    const CoinParams *p = &params;
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this, tree, p](const QPoint &pos) {
        QTreeWidgetItem *item = tree->itemAt(pos);
        CoinWallet *coin = m_vault->wallet(*p);
        if (!item || !coin) {
            return;
        }
        const QStringList keys = selectedKeys(tree);
        const QString key = item->data(ColAmount, KeyRole).toString();
        const QString txid = key.section(':', 0, 0);
        const QString count = keys.size() > 1 ? QString(" (%1 coins)").arg(keys.size()) : QString();

        QMenu menu(this);
        menu.addAction("Send these coins…" + count, [this, keys, p] {
            m_vault->setCoinSelection(*p, keys);
            emit sendCoins(m_picker->indexOf(p->ticker));
        });
        if (keys.size() >= 2) {
            menu.addAction("Consolidate these coins…" + count, [this, keys, p] {
                CoinSendController(m_wallet).consolidate(this, *p, keys);
            });
        }
        menu.addSeparator();
        menu.addAction("Freeze" + count, [coin, keys] { coin->setFrozen(keys, true); });
        menu.addAction("Unfreeze" + count, [coin, keys] { coin->setFrozen(keys, false); });
        menu.addSeparator();
        menu.addAction("Copy address", [item] { Utils::copyToClipboard(item->text(ColAddress)); });
        menu.addAction("Copy coin (txid:output)", [key] { Utils::copyToClipboard(key); });
        menu.addAction("View on block explorer", [this, p, txid] { Utils::externalLinkWarning(this, explorerTxUrl(*p, txid)); });
        menu.exec(tree->viewport()->mapToGlobal(pos));
    });
    return tree;
}

QStringList CoinCoinsSwitcher::selectedKeys(QTreeWidget *tree) const {
    QStringList keys;
    for (QTreeWidgetItem *item : tree->selectedItems()) {
        keys.append(item->data(ColAmount, KeyRole).toString());
    }
    return keys;
}

void CoinCoinsSwitcher::refresh() {
    for (const CoinParams *params : electrumCoins()) {
        fillTree(m_trees.value(params->ticker), m_summaries.value(params->ticker), *params);
    }
}

void CoinCoinsSwitcher::fillTree(QTreeWidget *tree, QLabel *summary, const CoinParams &params) {
    tree->setSortingEnabled(false);
    tree->clear();
    CoinWallet *coin = m_vault->isUnlocked() ? m_vault->wallet(params) : nullptr;
    if (!coin) {
        summary->setText(m_vault->coinState(params) == CoinVault::CoinState::Locked
                         ? QString("Unlock %1 to see its coins.").arg(params.name)
                         : QString("%1 is not in this wallet. Add it with + in Receive.").arg(params.name));
        tree->setSortingEnabled(true);
        return;
    }
    auto format = [&params](quint64 sats) {
        return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(sats, params.decimals), params.ticker);
    };
    const QStringList chosen = m_vault->coinSelection(params);
    const int tip = coin->blockHeight();
    // Dust: worth no more than what spending it costs at the normal fee now.
    const double normalRate = coin->feeRate(6);
    quint64 total = 0, frozenTotal = 0, dustTotal = 0;
    int frozenCount = 0, dustCount = 0;
    for (const auto &c : coin->coins()) {
        const Utxo &u = c.utxo;
        const bool frozen = coin->isFrozen(u);
        const QString key = CoinWallet::coinKey(u);
        auto *item = new CoinItem(tree);
        item->setText(ColAmount, format(u.value));
        item->setData(ColAmount, SortRole, QVariant::fromValue<qulonglong>(u.value));
        item->setData(ColAmount, KeyRole, key);
        item->setText(ColAddress, scriptAddress(c.scriptPubKey, params));
        QString status = u.height <= 0 ? QString("Unconfirmed")
                       : tip > 0 ? QString("%1 confirmations").arg(tip - u.height + 1) : QString("Confirmed");
        if (frozen) status = "Frozen";
        if (chosen.contains(key)) status += ", selected for sending";
        const bool dust = normalRate > 0 && isDust(params, u.value, normalRate);
        if (dust) {
            status += ", dust";
            item->setToolTip(ColStatus, QString("Worth less than the fee to spend it right now (%1 at %2). "
                                                "It can wait for lower fees, or stay where it is.")
                                        .arg(format(inputCost(params, normalRate)), feeRateText(params, normalRate)));
            ++dustCount;
            dustTotal += u.value;
        }
        item->setText(ColStatus, status);
        item->setText(ColCoin, key);
        item->setToolTip(ColCoin, key);
        if (frozen) {
            for (int col = 0; col < tree->columnCount(); ++col) {
                item->setForeground(col, tree->palette().brush(QPalette::Disabled, QPalette::Text));
            }
        }
        total += u.value;
        if (frozen) {
            frozenTotal += u.value;
            ++frozenCount;
        }
    }
    tree->setSortingEnabled(true);
    QString text = QString("%1 coins, %2").arg(tree->topLevelItemCount()).arg(format(total));
    if (frozenCount > 0) {
        text += QString(" · %1 frozen (%2), never spent unless you select them").arg(frozenCount).arg(format(frozenTotal));
    }
    if (dustCount > 0) {
        text += QString(" · %1 dust (%2): worth less than their fee right now").arg(dustCount).arg(format(dustTotal));
    }
    summary->setText(text + ". Right-click a coin to freeze it or send only the coins you choose.");
}

}
