// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "HomeWidget.h"
#include "ui_HomeWidget.h"

#include <algorithm>
#include <limits>

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Amount.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "coins/core/CoinParams.h"
#include "constants.h"
#include "libwalletqt/TransactionHistory.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"
#include "libwalletqt/rows/TransactionRow.h"
#include "utils/AppData.h"
#include "utils/Icons.h"
#include "utils/Utils.h"
#include "utils/config.h"

namespace {
    constexpr int recentCount = 5;
    constexpr int crowdfundingHeight = 230;   // about 6 rows, then it scrolls

    struct Activity {
        QDateTime time;       // invalid while unknown (sorted first, as pending)
        QString ticker;
        QString amount;       // "+0.0333 XMR"
        QString status;
    };

    QString statusText(bool pending, bool failed, quint64 confirmations, quint64 required) {
        if (failed) return "Failed";
        if (pending) return "Pending";
        if (confirmations < required) return QString("%1/%2 confirmations").arg(confirmations).arg(required);
        return "Confirmed";
    }

    QIcon coinIcon(const QString &ticker) {
        if (ticker == "XMR") return QIcon(":/assets/images/appicons/monero.png");
        return icons()->icon(ticker == "BTC" ? "bitcoin.png" : "litecoin.png");
    }
}

HomeWidget::HomeWidget(Wallet *wallet, QWidget *parent)
        : QWidget(parent)
        , ui(new Ui::HomeWidget)
        , m_wallet(wallet)
{
    ui->setupUi(this);

    // Biscuit: the dashboard. Total first, prices (plugins) next to it,
    // recent activity, then a short Crowdfunding list.
    auto *top = new QHBoxLayout;
    top->setSpacing(24);
    auto *totalBox = new QFrame(this);
    totalBox->setFrameShape(QFrame::StyledPanel);
    auto *totalLayout = new QVBoxLayout(totalBox);
    totalLayout->setContentsMargins(16, 10, 16, 10);
    totalLayout->setSpacing(2);
    auto *totalTitle = new QLabel("Total", totalBox);
    totalTitle->setStyleSheet("color: gray;");
    m_total = new QLabel(totalBox);
    QFont big = m_total->font();
    big.setPointSizeF(big.pointSizeF() * 2.2);
    big.setBold(true);
    m_total->setFont(big);
    m_total->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_coins = new QLabel(totalBox);
    m_coins->setStyleSheet("color: gray;");
    m_coins->setTextInteractionFlags(Qt::TextSelectableByMouse);
    totalLayout->addWidget(totalTitle);
    totalLayout->addWidget(m_total);
    totalLayout->addWidget(m_coins);
    top->addWidget(totalBox, 0, Qt::AlignTop);
    // The price tickers (plugins) go to the right of the total.
    ui->verticalLayout->removeItem(ui->widgetLayout);
    top->addLayout(ui->widgetLayout, 1);
    ui->verticalLayout->insertLayout(0, top);

    auto *recentHeader = new QHBoxLayout;
    auto *recentTitle = new QLabel("Recent activity", this);
    QFont bold = recentTitle->font();
    bold.setBold(true);
    recentTitle->setFont(bold);
    auto *seeAll = new QPushButton("See all", this);
    seeAll->setAutoDefault(false);
    connect(seeAll, &QPushButton::clicked, this, &HomeWidget::showHistoryTab);
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch();
    recentHeader->addWidget(seeAll);
    m_recent = new QTreeWidget(this);
    m_recent->setRootIsDecorated(false);
    m_recent->setUniformRowHeights(true);
    m_recent->setHeaderLabels({"Date", "Coin", "Amount", "Status"});
    m_recent->header()->setStretchLastSection(true);
    m_recent->setSelectionMode(QAbstractItemView::NoSelection);
    m_recent->setFocusPolicy(Qt::NoFocus);
    m_recent->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_recent, &QTreeWidget::itemDoubleClicked, this, &HomeWidget::showHistoryTab);
    m_recentEmpty = new QLabel("No transactions yet.", this);
    m_recentEmpty->setStyleSheet("color: gray;");
    ui->verticalLayout->insertSpacing(1, 12);
    ui->verticalLayout->insertLayout(2, recentHeader);
    ui->verticalLayout->insertWidget(3, m_recent);
    ui->verticalLayout->insertWidget(4, m_recentEmpty);
    ui->verticalLayout->insertSpacing(5, 12);

    // Crowdfunding: a short list rather than a full-page table.
    ui->tabHomeWidget->setMaximumHeight(crowdfundingHeight);
    ui->verticalLayout->addStretch(1);

    if (m_wallet) {
        connect(m_wallet, &Wallet::balanceUpdated, this, &HomeWidget::updateTotal);
        connect(m_wallet->history(), &TransactionHistory::refreshFinished, this, &HomeWidget::updateRecent);
        auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
        for (auto signal : {&biscuit::coins::CoinVault::unlocked, &biscuit::coins::CoinVault::locked,
                            &biscuit::coins::CoinVault::walletsChanged, &biscuit::coins::CoinVault::walletUpdated}) {
            connect(vault, signal, this, &HomeWidget::updateTotal);
            connect(vault, signal, this, &HomeWidget::updateRecent);
        }
    }
    connect(&appData()->prices, &Prices::fiatPricesUpdated, this, &HomeWidget::updateTotal);
    connect(&appData()->prices, &Prices::cryptoPricesUpdated, this, &HomeWidget::updateTotal);
    updateTotal();
    updateRecent();
}

void HomeWidget::updateTotal() {
    if (!m_wallet) {
        return;
    }
    const bool hide = conf()->get(Config::hideBalance).toBool();
    const QString fiat = conf()->get(Config::preferredFiatCurrency).toString();
    auto &prices = appData()->prices;
    const quint64 xmr = m_wallet->balance();
    bool known = prices.canConvert("XMR", fiat);
    double total = prices.convert("XMR", fiat, xmr / constants::cdiv);
    QStringList coins{QString("%1 XMR").arg(WalletManager::displayAmount(xmr, false))};

    auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
    if (vault->isUnlocked()) {
        for (const auto *params : {&biscuit::coins::bitcoin(), &biscuit::coins::litecoin()}) {
            quint64 amount = 0;
            for (const auto &entry : vault->wallets(*params)) {
                amount += entry.wallet->balance().total();
            }
            coins << QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(amount, params->decimals), params->ticker);
            if (amount > 0) {
                known = known && prices.canConvert(params->ticker, fiat);
            }
            total += prices.convert(params->ticker, fiat, double(amount) / 1e8);
        }
    }

    if (hide) {
        m_total->setText("Hidden");
        m_coins->clear();
        return;
    }
    m_total->setText(known ? Utils::amountToCurrencyString(total, fiat) : QString("–"));
    m_total->setToolTip(known ? QString("Every coin and wallet together, at market prices (%1, changeable in Settings).").arg(fiat)
                              : QString("Prices are not loaded yet."));
    m_coins->setText(coins.join(" · "));
}

void HomeWidget::updateRecent() {
    if (!m_wallet) {
        return;
    }
    QList<Activity> all;
    for (const TransactionRow &row : m_wallet->history()->getRows()) {
        all.append({row.pending ? QDateTime() : row.timestamp, "XMR",
                    QString("%1%2 XMR").arg(row.balanceDelta < 0 ? "-" : "+",
                                            WalletManager::displayAmount(quint64(std::llabs(row.balanceDelta)), false)),
                    statusText(row.pending, row.failed, row.confirmations, row.confirmationsRequired())});
    }
    auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
    if (vault->isUnlocked()) {
        for (auto *coin : vault->allWallets()) {
            const auto &params = coin->params();
            const int tip = coin->blockHeight();
            for (const auto &e : coin->history()) {
                const quint64 confirmations = e.height > 0 && tip > 0 ? quint64(tip - e.height + 1) : 0;
                all.append({e.height > 0 ? coin->transactionTime(e.txid) : QDateTime(), params.ticker,
                            QString("%1%2 %3").arg(e.delta < 0 ? "-" : "+",
                                                   biscuit::swap::amount::fromAtomic(quint64(std::llabs(e.delta)), params.decimals),
                                                   params.ticker),
                            statusText(e.height <= 0, false, confirmations, 6)});
            }
        }
    }
    // Newest first, pending (no time yet) on top.
    std::stable_sort(all.begin(), all.end(), [](const Activity &a, const Activity &b) {
        const qint64 ta = a.time.isValid() ? a.time.toSecsSinceEpoch() : std::numeric_limits<qint64>::max();
        const qint64 tb = b.time.isValid() ? b.time.toSecsSinceEpoch() : std::numeric_limits<qint64>::max();
        return ta > tb;
    });

    m_recent->clear();
    for (int i = 0; i < std::min<int>(recentCount, all.size()); ++i) {
        const Activity &a = all.at(i);
        auto *item = new QTreeWidgetItem(m_recent);
        item->setText(0, a.time.isValid() ? a.time.toLocalTime().toString("yyyy-MM-dd HH:mm") : QString("Pending"));
        item->setIcon(1, coinIcon(a.ticker));
        item->setText(1, a.ticker);
        item->setText(2, a.amount);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(3, a.status);
    }
    for (int c = 0; c < 3; ++c) {
        m_recent->resizeColumnToContents(c);
    }
    // Exactly as tall as its rows: no empty lines.
    const int rows = m_recent->topLevelItemCount();
    m_recent->setVisible(rows > 0);
    m_recentEmpty->setVisible(rows == 0);
    if (rows > 0) {
        const int rowHeight = m_recent->sizeHintForRow(0);
        m_recent->setFixedHeight(m_recent->header()->sizeHint().height() + rows * rowHeight + 2 * m_recent->frameWidth());
    }
}

// "See all": the History tab of the main window.
void HomeWidget::showHistoryTab() {
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i).remove('&') == "History") {
                    tabs->setCurrentIndex(i);
                    return;
                }
            }
        }
    }
}

void HomeWidget::addPlugin(Plugin *plugin)
{
    if (plugin->type() == Plugin::TAB) {
        ui->tabHomeWidget->addTab(plugin->tab(), plugin->displayName());
    }
    else if (plugin->type() == Plugin::WIDGET) {
        ui->widgetLayout->addWidget(plugin->tab());
    }
}

void HomeWidget::uiSetup() {
    ui->tabHomeWidget->setCurrentIndex(conf()->get(Config::homeWidget).toInt());
}

void HomeWidget::aboutToQuit() {
    conf()->set(Config::homeWidget, ui->tabHomeWidget->currentIndex());
}

HomeWidget::~HomeWidget() = default;
