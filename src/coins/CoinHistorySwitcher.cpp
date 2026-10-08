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
#include "CoinSendController.h"
#include "CoinWalletBar.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "libwalletqt/TransactionHistory.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"
#include "utils/Icons.h"
#include "utils/Utils.h"
#include "utils/config.h"
#include "widgets/PixelIcons.h"

namespace biscuit::coins {

namespace {
    enum Column { ColDate = 0, ColCoin, ColWallet, ColAmount, ColStatus, ColTx };
    enum Page { PageAll = 0, PageMonero, PageBitcoin, PageLitecoin };
    constexpr int TickerRole = Qt::UserRole;
    constexpr int SortRole = Qt::UserRole + 1;
    constexpr int WalletIdRole = Qt::UserRole + 2;   // Bitcoin/Litecoin rows: which wallet
    constexpr int BumpableRole = Qt::UserRole + 3;   // our unconfirmed payment: can be sped up

    QString explorerUrl(const QString &ticker, const QString &txid) {
        if (ticker == QLatin1String("BTC")) return QString("https://mempool.space/tx/%1").arg(txid);
        if (ticker == QLatin1String("LTC")) return QString("https://litecoinspace.org/tx/%1").arg(txid);
        if (coinOfTicker(ticker) == &ethereum()) return QString("https://eth.blockscout.com/tx/%1").arg(txid);
        return QString("https://xmrchain.net/tx/%1").arg(txid);
    }

    QIcon assetIcon(const QString &ticker) {
        if (ticker == QLatin1String("ETH")) return PixelIcons::icon("ethereum");
        if (ticker == QLatin1String("USDT")) return PixelIcons::icon("tether");
        if (ticker == QLatin1String("USDC")) return PixelIcons::icon("usdc");
        return {};
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
    m_filter->addCoin(QIcon(), "All coins", "*");
    addWalletCoins(m_filter, true);
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
    // Ethereum, then its tokens: one page each, same order as the tabs.
    for (const QString &asset : EthWallet::assets()) {
        auto *tree = makeTree(false);
        m_ethTrees.insert(asset, tree);
        auto *page = new QWidget(m_pages);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        auto *bar = new CoinWalletBar(m_vault, ethereum(), page);
        bar->setAsset(asset);
        pageLayout->addWidget(bar);
        pageLayout->addWidget(tree);
        m_pages->addWidget(page);
    }
    layout->addWidget(m_pages);

    connect(m_filter, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    showWalletCoins(m_filter, m_vault, false);
    connect(m_wallet->history(), &TransactionHistory::refreshFinished, this, &CoinHistorySwitcher::refresh);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged, &CoinVault::walletUpdated}) {
        connect(m_vault, signal, this, &CoinHistorySwitcher::refresh);
    }
    // Fee values in the tooltips: in the preferred currency.
    connect(conf(), &Config::changed, this, [this](Config::ConfigKey key) {
        if (key == Config::preferredFiatCurrency) refresh();
    });
    refresh();
}

QTreeWidget *CoinHistorySwitcher::makeTree(bool withCoinColumn) {
    auto *tree = new QTreeWidget(this);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setHeaderLabels({"Date", "Coin", "Wallet", "Amount", "Status", "Transaction"});
    tree->setColumnHidden(ColCoin, !withCoinColumn);
    tree->setColumnHidden(ColWallet, true);   // shown in "All coins" when a coin has several wallets
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
        if (item->data(ColDate, BumpableRole).toBool() && coinOfTicker(ticker) == &ethereum()) {
            // Ethereum: same nonce, higher fee.
            const QString walletId = item->data(ColDate, WalletIdRole).toString();
            const QByteArray hash = QByteArray::fromHex(txid.mid(2).toLatin1());
            menu.addAction("Speed up (raise the fee)…", [this, walletId, hash] {
                for (const auto &entry : m_vault->wallets(ethereum())) {
                    if (entry.id == walletId) {
                        CoinSendController(m_wallet).speedUpEthereum(this, entry.eth, hash);
                    }
                }
            });
            menu.addSeparator();
        } else if (item->data(ColDate, BumpableRole).toBool()) {
            const QString walletId = item->data(ColDate, WalletIdRole).toString();
            const CoinParams *params = ticker == bitcoin().ticker ? &bitcoin() : &litecoin();
            menu.addAction("Speed up (raise the fee)…", [this, params, walletId, txid] {
                for (const auto &entry : m_vault->wallets(*params)) {
                    if (entry.id == walletId) {
                        CoinSendController(m_wallet).bumpFee(this, *params, entry.wallet, txid);
                    }
                }
            });
            menu.addSeparator();
        }
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
                item->setText(ColCoin, params.ticker);
                item->setText(ColWallet, entry.name);
                item->setText(ColAmount, amount);
                item->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
                item->setText(ColStatus, statusText(e.height <= 0, false, confirmations, 6));
                item->setText(ColTx, e.txid);
                item->setData(ColDate, WalletIdRole, entry.id);
                item->setData(ColDate, BumpableRole, e.height <= 0 && e.fee.has_value() && e.delta < 0);
            }
        }
    }
}

void CoinHistorySwitcher::fillEthereumTrees() {
    if (!m_vault->isUnlocked()) {
        return;
    }
    // Like the other coins: "All coins" lists every wallet, a page the selected one.
    const QString selectedId = m_vault->selectedId(ethereum());
    for (const auto &entry : m_vault->wallets(ethereum())) {
        for (const eth::HistoryEntry &e : entry.eth->history()) {
            QTreeWidget *page = m_ethTrees.value(e.asset);
            const bool shown = m_vault->assetState(e.asset) == CoinVault::CoinState::Ready
                               || e.amount > 0;   // a token not added, but received: still money
            if (!page || !shown) {
                continue;
            }
            const int decimals = EthWallet::decimals(e.asset);
            const QString amount = QString("%1%2 %3").arg(e.incoming ? "+" : "-", eth::formatAmount(e.amount, decimals), e.asset);
            const QString status = e.failed ? QString("Failed") : e.block == 0 ? QString("Pending") : QString("Confirmed");
            const QString hash = "0x" + QString::fromLatin1(e.hash.toHex());
            for (QTreeWidget *t : {page, m_all}) {
                if (t == page && entry.id != selectedId) {
                    continue;
                }
                auto *item = new DateItem(t);
                item->setText(ColDate, e.time.isValid() ? e.time.toLocalTime().toString("yyyy-MM-dd HH:mm") : QString());
                item->setData(ColDate, SortRole, e.time.isValid() ? e.time.toSecsSinceEpoch() : std::numeric_limits<qint64>::max());
                item->setData(ColDate, TickerRole, e.asset);
                item->setIcon(ColCoin, assetIcon(e.asset));
                item->setText(ColCoin, e.asset);
                item->setText(ColWallet, entry.name);
                item->setText(ColAmount, amount);
                item->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
                if (!e.incoming && e.fee > 0) {
                    const QString fee = eth::formatAmount(e.fee, eth::etherDecimals);
                    item->setToolTip(ColAmount, QString("Network fee: %1 ETH%2").arg(fee, fiatValue("ETH", fee)));
                }
                item->setText(ColStatus, status);
                item->setText(ColTx, hash);
                item->setData(ColDate, WalletIdRole, entry.id);
                item->setData(ColDate, BumpableRole, e.block == 0 && entry.eth->canSpeedUp(e.hash));
            }
        }
    }
}

void CoinHistorySwitcher::refresh() {
    QList<QTreeWidget *> trees{m_all, m_btc, m_ltc};
    trees << m_ethTrees.values();
    for (QTreeWidget *t : trees) {
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
            item->setIcon(ColCoin, PixelIcons::icon("monero"));
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
    fillEthereumTrees();
    m_all->setColumnHidden(ColWallet, !(m_vault->isUnlocked()
                                        && (m_vault->wallets(bitcoin()).size() > 1 || m_vault->wallets(litecoin()).size() > 1
                                            || m_vault->wallets(ethereum()).size() > 1)));

    for (QTreeWidget *t : trees) {
        t->setSortingEnabled(true);
        t->sortByColumn(ColDate, Qt::DescendingOrder);
        for (int c = ColDate; c < ColTx; ++c) t->resizeColumnToContents(c);
    }
}

}
