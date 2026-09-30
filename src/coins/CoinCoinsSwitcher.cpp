// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinCoinsSwitcher.h"

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Addresses.h"
#include "Amount.h"
#include "CoinPicker.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "CoinWalletBar.h"
#include "libwalletqt/Wallet.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Column { ColAmount = 0, ColAddress, ColStatus, ColCoin };
    enum Page { PageMonero = 0, PageBitcoin, PageLitecoin };
    constexpr int KeyRole = Qt::UserRole;
    constexpr int SortRole = Qt::UserRole + 1;

    QString explorerUrl(const CoinParams &params, const QString &txid) {
        return params == bitcoin() ? QString("https://mempool.space/tx/%1").arg(txid)
                                   : QString("https://litecoinspace.org/tx/%1").arg(txid);
    }

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
    m_btc = makeTree(bitcoin());
    m_ltc = makeTree(litecoin());
    m_btcSummary = new QLabel(this);
    m_ltcSummary = new QLabel(this);
    for (auto [tree, summary, params] : {std::tuple{m_btc, m_btcSummary, &bitcoin()},
                                         std::tuple{m_ltc, m_ltcSummary, &litecoin()}}) {
        auto *page = new QWidget(m_pages);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new CoinWalletBar(m_vault, *params, page));
        pageLayout->addWidget(tree);
        summary->setParent(page);
        pageLayout->addWidget(summary);
        m_pages->addWidget(page);
    }
    layout->addWidget(m_pages);

    connect(m_picker, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
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
            emit sendCoins(*p == bitcoin() ? PageBitcoin : PageLitecoin);
        });
        menu.addSeparator();
        menu.addAction("Freeze" + count, [coin, keys] { coin->setFrozen(keys, true); });
        menu.addAction("Unfreeze" + count, [coin, keys] { coin->setFrozen(keys, false); });
        menu.addSeparator();
        menu.addAction("Copy address", [item] { Utils::copyToClipboard(item->text(ColAddress)); });
        menu.addAction("Copy coin (txid:output)", [key] { Utils::copyToClipboard(key); });
        menu.addAction("View on block explorer", [this, p, txid] { Utils::externalLinkWarning(this, explorerUrl(*p, txid)); });
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
    fillTree(m_btc, m_btcSummary, bitcoin());
    fillTree(m_ltc, m_ltcSummary, litecoin());
}

void CoinCoinsSwitcher::fillTree(QTreeWidget *tree, QLabel *summary, const CoinParams &params) {
    tree->setSortingEnabled(false);
    tree->clear();
    CoinWallet *coin = m_vault->isUnlocked() ? m_vault->wallet(params) : nullptr;
    if (!coin) {
        summary->setText(QString("Unlock %1 to see its coins.").arg(params.name));
        tree->setSortingEnabled(true);
        return;
    }
    auto format = [&params](quint64 sats) {
        return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(sats, params.decimals), params.ticker);
    };
    const QStringList chosen = m_vault->coinSelection(params);
    const int tip = coin->blockHeight();
    quint64 total = 0, frozenTotal = 0;
    int frozenCount = 0;
    for (const auto &c : coin->coins()) {
        const Utxo &u = c.utxo;
        const bool frozen = coin->isFrozen(u);
        const QString key = CoinWallet::coinKey(u);
        auto *item = new CoinItem(tree);
        item->setText(ColAmount, format(u.value));
        item->setData(ColAmount, SortRole, QVariant::fromValue<qulonglong>(u.value));
        item->setData(ColAmount, KeyRole, key);
        item->setText(ColAddress, segwitAddress(c.scriptPubKey, params));
        QString status = u.height <= 0 ? QString("Unconfirmed")
                       : tip > 0 ? QString("%1 confirmations").arg(tip - u.height + 1) : QString("Confirmed");
        if (frozen) status = "Frozen";
        if (chosen.contains(key)) status += ", selected for sending";
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
    summary->setText(text + ". Right-click a coin to freeze it or send only the coins you choose.");
}

}
