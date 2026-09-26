// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinHistorySwitcher.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Amount.h"
#include "CoinPicker.h"
#include "CoinWalletBar.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "libwalletqt/TransactionHistory.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Column { ColDate = 0, ColCoin, ColAmount, ColStatus, ColTx };
    enum Page { PageAll = 0, PageMonero, PageBitcoin, PageLitecoin };
    constexpr int TickerRole = Qt::UserRole;
    constexpr int SortRole = Qt::UserRole + 1;

    QString explorerUrl(const QString &ticker, const QString &txid) {
        if (ticker == QLatin1String("BTC")) return QString("https://mempool.space/tx/%1").arg(txid);
        if (ticker == QLatin1String("LTC")) return QString("https://litecoinspace.org/tx/%1").arg(txid);
        return QString("https://xmrchain.net/tx/%1").arg(txid);
    }

    QString statusText(bool pending, bool failed, quint64 confirmations, quint64 required) {
        if (failed) return "Failed";
        if (pending || confirmations == 0) return "Unconfirmed";
        if (confirmations < required) return QString("%1/%2 confirmations").arg(confirmations).arg(required);
        return "Confirmed";
    }

    // Keeps rows in date order, newest first, whatever the display format.
    class DateItem : public QTreeWidgetItem {
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
        bool operator<(const QTreeWidgetItem &other) const override {
            return data(ColDate, SortRole).toLongLong() < other.data(ColDate, SortRole).toLongLong();
        }
    };
}

CoinHistorySwitcher::CoinHistorySwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent)
    : QWidget(parent)
    , m_wallet(wallet)
    , m_vault(CoinVault::forWallet(wallet))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_filter = new CoinPicker(this);
    m_filter->addCoin(QIcon(), "All coins");
    addWalletCoins(m_filter);
    layout->addWidget(m_filter);

    m_all = makeTree(true);
    m_btc = makeTree(false);
    m_ltc = makeTree(false);
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(m_all);
    m_pages->addWidget(moneroPage);
    for (auto [tree, params] : {std::pair{m_btc, &bitcoin()}, std::pair{m_ltc, &litecoin()}}) {
        auto *page = new QWidget(m_pages);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new CoinWalletBar(m_vault, *params, page));
        pageLayout->addWidget(tree);
        m_pages->addWidget(page);
    }
    layout->addWidget(m_pages);

    connect(m_filter, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_wallet->history(), &TransactionHistory::refreshFinished, this, &CoinHistorySwitcher::refresh);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged, &CoinVault::walletUpdated}) {
        connect(m_vault, signal, this, &CoinHistorySwitcher::refresh);
    }
    refresh();
}

QTreeWidget *CoinHistorySwitcher::makeTree(bool withCoinColumn) {
    auto *tree = new QTreeWidget(this);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setHeaderLabels({"Date", "Coin", "Amount", "Status", "Transaction"});
    tree->setColumnHidden(ColCoin, !withCoinColumn);
    tree->header()->setSectionResizeMode(ColTx, QHeaderView::Stretch);
    tree->header()->setStretchLastSection(false);
    tree->setSortingEnabled(true);
    tree->sortByColumn(ColDate, Qt::DescendingOrder);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this, tree](const QPoint &pos) {
        auto *item = tree->itemAt(pos);
        if (!item) return;
        const QString txid = item->text(ColTx);
        const QString ticker = item->data(ColDate, TickerRole).toString();
        QMenu menu(this);
        menu.addAction("Copy transaction ID", [txid] { Utils::copyToClipboard(txid); });
        menu.addAction("View on block explorer", [this, ticker, txid] {
            Utils::externalLinkWarning(this, explorerUrl(ticker, txid));
        });
        menu.exec(tree->viewport()->mapToGlobal(pos));
    });
    return tree;
}

void CoinHistorySwitcher::fillCoinTree(QTreeWidget *tree, const CoinParams &params) {
    if (!m_vault->isUnlocked()) {
        return;
    }
    // "All coins" lists every wallet of the coin; the coin page, the selected one.
    const auto entries = m_vault->wallets(params);
    const QString selectedId = m_vault->selectedId(params);
    for (const auto &entry : entries) {
        CoinWallet *coin = entry.wallet;
        const QString coinLabel = entries.size() > 1 ? QString("%1 · %2").arg(params.ticker, entry.name) : params.ticker;
        const int tip = coin->blockHeight();
        for (const auto &e : coin->history()) {
            const QDateTime time = coin->transactionTime(e.txid);
            const quint64 confirmations = e.height > 0 && tip > 0 ? quint64(tip - e.height + 1) : 0;
            const QString amount = QString("%1%2 %3").arg(e.delta < 0 ? "-" : "+",
                    biscuit::swap::amount::fromAtomic(quint64(std::llabs(e.delta)), params.decimals), params.ticker);
            for (QTreeWidget *t : {tree, m_all}) {
                if (t == tree && entry.id != selectedId) {
                    continue;
                }
                auto *item = new DateItem(t);
                item->setText(ColDate, time.isValid() ? time.toLocalTime().toString("yyyy-MM-dd HH:mm") : QString());
                item->setData(ColDate, SortRole, time.isValid() ? time.toSecsSinceEpoch() : std::numeric_limits<qint64>::max());
                item->setData(ColDate, TickerRole, params.ticker);
                item->setIcon(ColCoin, icons()->icon(params == bitcoin() ? "bitcoin.png" : "litecoin.png"));
                item->setText(ColCoin, coinLabel);
                item->setText(ColAmount, amount);
                item->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
                item->setText(ColStatus, statusText(e.height <= 0, false, confirmations, 6));
                item->setText(ColTx, e.txid);
            }
        }
    }
}

void CoinHistorySwitcher::refresh() {
    for (QTreeWidget *t : {m_all, m_btc, m_ltc}) {
        t->setSortingEnabled(false);
        t->clear();
    }

    // Monero, from Feather's transaction history.
    if (m_wallet) {
        for (const TransactionRow &row : m_wallet->history()->getRows()) {
            auto *item = new DateItem(m_all);
            item->setText(ColDate, row.timestamp.toString("yyyy-MM-dd HH:mm"));
            item->setData(ColDate, SortRole, row.pending ? std::numeric_limits<qint64>::max() : row.timestamp.toSecsSinceEpoch());
            item->setData(ColDate, TickerRole, "XMR");
            item->setIcon(ColCoin, QIcon(":/assets/images/appicons/monero.png"));
            item->setText(ColCoin, "XMR");
            item->setText(ColAmount, QString("%1%2 XMR").arg(row.balanceDelta < 0 ? "-" : "+",
                    WalletManager::displayAmount(quint64(std::llabs(row.balanceDelta)), false)));
            item->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
            item->setText(ColStatus, statusText(row.pending, row.failed, row.confirmations, row.confirmationsRequired()));
            item->setText(ColTx, row.hash);
        }
    }
    fillCoinTree(m_btc, bitcoin());
    fillCoinTree(m_ltc, litecoin());

    for (QTreeWidget *t : {m_all, m_btc, m_ltc}) {
        t->setSortingEnabled(true);
        t->sortByColumn(ColDate, Qt::DescendingOrder);
        for (int c = ColDate; c < ColTx; ++c) t->resizeColumnToContents(c);
    }
}

}
