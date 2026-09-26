// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapWidget.h"
#include "ui_SwapWidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

#include "Amount.h"
#include "AtomicSwapWidget.h"
#include "DemoSwap.h"
#include "SwapTradeDialog.h"
#include "coins/CoinSendController.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "coins/core/CoinParams.h"
#include "components.h"
#include "swap/TrocadorSwapProvider.h"
#include "utils/config.h"
#include "constants.h"
#include "libwalletqt/Subaddress.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

using namespace biscuit::swap;
using biscuit::swap::TrocadorSwapProvider;

namespace {
    enum OfferColumn { OfferExchange = 0, OfferGet, OfferEta, OfferVia };

    // Only exchanges rated A by Trocador: no KYC, no identity checks.
    constexpr KycRating noKyc = KycRating::A;
    enum TradeColumn { TradeDate = 0, TradePair, TradeSent, TradeReceived, TradeStatusCol, TradeExchange };

    constexpr int QuoteIndexRole = Qt::UserRole;
    constexpr int ProviderRole = Qt::UserRole;
    constexpr int TradeIdRole = Qt::UserRole + 1;

    // "Receive at" / "Refund to" choices when the coin is XMR.
    constexpr int ModeNewWalletAddress = 0;
    constexpr int ModeOtherAddress = 1;

    constexpr int moneroDecimals = 12;
}

SwapWidget::SwapWidget(Wallet *wallet, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SwapWidget)
    , m_wallet(wallet)
    , m_manager(new SwapManager(wallet, this))
    , m_coins(new biscuit::coins::CoinSendController(wallet, this))
{
    ui->setupUi(this);
    // Atomic swaps (BTC -> XMR, public makers) sit next to the exchange swaps.
    m_atomicTab = new AtomicSwapWidget(this);
    ui->tabWidget->insertTab(1, m_atomicTab, "Atomic swap (BTC → XMR)");

    const bool demo = m_manager->demoMode();
    ui->frame_demo->setVisible(demo);
    // The demo and Tor notices are about exchange swaps, not atomic swaps.
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &SwapWidget::updateTorMode);
    if (demo) {
        ui->frame_demo->setInfo(icons()->icon("info2.svg"),
                                "Demo mode: this build has no swap partner key. Offers are simulated, "
                                "no exchange is contacted and nothing can be sent.");
    }

    // Shown in Tor mode, where exchange swaps are off.
    m_torNotice = new InfoFrame(this);
    m_torNotice->setFrameShape(QFrame::StyledPanel);
    m_torNotice->setInfo(icons()->icon("info2.svg"),
                         "Tor mode is on: exchange swaps are not available, because Trocador and its exchanges "
                         "do not accept Tor connections. Atomic swaps work over Tor. Swaps already started "
                         "still complete at the exchange; their status updates resume when Tor mode is off.");
    ui->verticalLayout->insertWidget(1, m_torNotice);

    // Swap partners work on mainnet only: a stagenet or testnet address would
    // lose the funds. The offline demo stays available for development.
    m_mainnet = constants::networkType == NetworkType::MAINNET || demo;
    if (!m_mainnet) {
        ui->label_status->setText("Swaps are only available on mainnet.");
    }

    ui->combo_rate->setItemData(0, "You choose the amount you send. The amount you get may vary slightly with the market.", Qt::ToolTipRole);
    ui->combo_rate->setItemData(1, "You choose the exact amount you get. Each exchange says how much to send, usually at a slightly lower rate.", Qt::ToolTipRole);

    ui->label_transparency->setStyleSheet("color: gray;");
    ui->label_historyHint->setStyleSheet("color: gray;");

    ui->tree_offers->header()->setSectionResizeMode(OfferExchange, QHeaderView::Stretch);
    ui->tree_offers->header()->setStretchLastSection(false);
    ui->tree_trades->header()->setSectionResizeMode(TradeStatusCol, QHeaderView::Stretch);
    ui->tree_trades->header()->setStretchLastSection(false);

    connect(ui->combo_from, &QComboBox::currentIndexChanged, this, &SwapWidget::onFromChanged);
    connect(ui->combo_to, &QComboBox::currentIndexChanged, this, &SwapWidget::onToChanged);
    connect(ui->btn_reverse, &QPushButton::clicked, this, &SwapWidget::onReverse);
    connect(ui->line_amount, &QLineEdit::textEdited, this, &SwapWidget::clearOffers);
    connect(ui->combo_rate, &QComboBox::currentIndexChanged, this, [this] {
        placeAmountField();
        clearOffers();
    });
    connect(ui->combo_receiveMode, &QComboBox::currentIndexChanged, this, &SwapWidget::updateForm);
    connect(ui->combo_refundMode, &QComboBox::currentIndexChanged, this, &SwapWidget::updateForm);
    connect(ui->btn_clear, &QPushButton::clicked, this, &SwapWidget::onClear);
    connect(ui->btn_offers, &QPushButton::clicked, this, &SwapWidget::onGetOffers);
    connect(ui->line_amount, &QLineEdit::returnPressed, this, &SwapWidget::onGetOffers);
    connect(ui->btn_create, &QPushButton::clicked, this, &SwapWidget::onCreateSwap);
    connect(ui->tree_offers, &QTreeWidget::itemSelectionChanged, this, [this] {
        ui->btn_create->setEnabled(!ui->tree_offers->selectedItems().isEmpty());
    });

    connect(ui->tree_trades, &QTreeWidget::itemDoubleClicked, this, &SwapWidget::onShowTradeDetails);
    connect(ui->tree_trades, &QTreeWidget::itemSelectionChanged, this, [this] {
        ui->btn_details->setEnabled(!ui->tree_trades->selectedItems().isEmpty());
    });
    connect(ui->btn_details, &QPushButton::clicked, this, &SwapWidget::onShowTradeDetails);
    connect(ui->btn_refresh, &QPushButton::clicked, m_manager, &SwapManager::refreshNow);
    connect(m_manager, &SwapManager::tradesChanged, this, &SwapWidget::refreshTrades);

    // Record the txid when the user confirms a deposit sent from this wallet.
    connect(m_wallet, &Wallet::transactionCommitted, this,
            [this](bool success, PendingTransaction *, const QStringList &txids, const QMap<QString, QString> &) {
        if (m_pendingDepositTrade.isEmpty()) {
            return;
        }
        if (success && !txids.isEmpty() && m_wallet->tmpTxDescription == m_pendingDepositDescription) {
            m_manager->setDepositTx(m_pendingDepositProvider, m_pendingDepositTrade, txids.first());
        }
        m_pendingDepositTrade.clear();
    });

    updateTorMode();
    loadAssets();
    updateForm();
    refreshTrades();
}

void SwapWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateTorMode();   // the proxy settings may have changed
}

void SwapWidget::updateTorMode() {
    if (!m_torNotice) {
        return;   // still in the constructor
    }
    const bool torMode = !m_manager->demoMode() && TrocadorSwapProvider::proxyActive();
    const bool onAtomicTab = ui->tabWidget->currentWidget() == m_atomicTab;

    ui->frame_demo->setVisible(m_manager->demoMode() && !onAtomicTab);
    m_torNotice->setVisible(torMode && !onAtomicTab);
    ui->tab_new->setEnabled(m_mainnet && !torMode);
    ui->btn_offers->setEnabled(!TrocadorSwapProvider::keyRejected());
    if (TrocadorSwapProvider::keyRejected()) {
        ui->label_status->setText("Swaps are unavailable in this version of Biscuit. Please update Biscuit.");
    }
}

SwapWidget::~SwapWidget() = default;

// ------------------------------------------------------------------- assets

Asset SwapWidget::xmr() const {
    return {"xmr", "Mainnet"};
}

Asset SwapWidget::assetOf(const QComboBox *combo) {
    const QStringList parts = combo->currentData().toStringList();
    return parts.size() == 2 ? Asset{parts.at(0), parts.at(1)} : Asset{};
}

void SwapWidget::selectAsset(QComboBox *combo, const Asset &asset) {
    const int index = combo->findData(QStringList{asset.ticker, asset.network});
    if (index >= 0) {
        combo->setCurrentIndex(index);
    }
}

bool SwapWidget::sendsXmr() const {
    return assetOf(ui->combo_from) == xmr();
}

bool SwapWidget::receivesXmr() const {
    return assetOf(ui->combo_to) == xmr();
}

bool SwapWidget::fixedRate() const {
    return ui->combo_rate->currentIndex() == 1;
}

// Floating rate: the amount typed is the one sent. Fixed rate: it is the exact
// amount received (as in Cake Wallet), and the estimate shows what to send.
void SwapWidget::placeAmountField() {
    ui->layout_sendAmount->removeWidget(ui->line_amount);
    ui->layout_get->removeWidget(ui->line_amount);
    ui->layout_send->removeWidget(ui->label_estimate);
    ui->layout_get->removeWidget(ui->label_estimate);
    if (fixedRate()) {
        ui->layout_get->insertWidget(0, ui->line_amount);
        ui->layout_send->insertWidget(2, ui->label_estimate);   // after the ⇄ button
        ui->layout_get->setSpacing(0);
    } else {
        ui->layout_sendAmount->insertWidget(0, ui->line_amount);
        ui->layout_get->insertWidget(1, ui->label_estimate);    // after the coin
        ui->layout_get->setSpacing(12);
    }
    ui->tree_offers->headerItem()->setText(OfferGet, fixedRate() ? "You send" : "You get");
}

void SwapWidget::loadAssets() {
    const auto providers = m_manager->providers();
    if (providers.isEmpty()) {
        ui->label_status->setText("All swap providers are disabled in the settings.");
        return;
    }
    // One provider for now. With several, their lists will be merged.
    providers.first()->supportedAssets([this](const QList<AssetInfo> &partnerAssets, const QString &error) {
        if (!error.isEmpty()) {
            ui->label_status->setText("Unable to load coins: " + error);
            return;
        }
        const QList<AssetInfo> assets = walletAssets(partnerAssets);
        m_updating = true;
        for (QComboBox *combo : {ui->combo_from, ui->combo_to}) {
            combo->clear();
            for (const AssetInfo &info : assets) {
                combo->addItem(info.asset.displayName(), QStringList{info.asset.ticker, info.asset.network});
                combo->setItemData(combo->count() - 1, info.name, Qt::ToolTipRole);
            }
        }
        selectAsset(ui->combo_from, xmr());
        ui->combo_to->setCurrentIndex(ui->combo_to->findData(QStringList{"btc", "Mainnet"}) >= 0
                                      ? ui->combo_to->findData(QStringList{"btc", "Mainnet"}) : 1);
        m_updating = false;
        updateForm();
    });
}

// --------------------------------------------------------------------- form

void SwapWidget::onFromChanged() {
    if (m_updating) {
        return;
    }
    m_updating = true;
    const Asset from = assetOf(ui->combo_from);
    const Asset to = assetOf(ui->combo_to);
    if (from != xmr() && to != xmr()) {
        selectAsset(ui->combo_to, xmr());
    } else if (from == xmr() && to == xmr()) {
        ui->combo_to->setCurrentIndex(ui->combo_to->currentIndex() == 0 ? 1 : 0);
    }
    m_updating = false;
    updateForm();
}

void SwapWidget::onToChanged() {
    if (m_updating) {
        return;
    }
    m_updating = true;
    const Asset from = assetOf(ui->combo_from);
    const Asset to = assetOf(ui->combo_to);
    if (from != xmr() && to != xmr()) {
        selectAsset(ui->combo_from, xmr());
    } else if (from == xmr() && to == xmr()) {
        ui->combo_from->setCurrentIndex(ui->combo_from->currentIndex() == 0 ? 1 : 0);
    }
    m_updating = false;
    updateForm();
}

void SwapWidget::onReverse() {
    const Asset from = assetOf(ui->combo_from);
    const Asset to = assetOf(ui->combo_to);
    m_updating = true;
    selectAsset(ui->combo_from, to);
    selectAsset(ui->combo_to, from);
    m_updating = false;
    // Addresses belong to the other side now.
    ui->line_receive->clear();
    ui->line_refund->clear();
    updateForm();
}

void SwapWidget::onClear() {
    ui->line_amount->clear();
    ui->line_receive->clear();
    ui->line_refund->clear();
    clearOffers();
}

void SwapWidget::updateForm() {
    const Asset from = assetOf(ui->combo_from);
    const Asset to = assetOf(ui->combo_to);
    const bool sendXmr = sendsXmr();

    // Every coin offered (XMR, BTC, LTC) has its wallet in Biscuit: coins are
    // received, and refunded, to this wallet by default, or to any address.
    // XMR: a new subaddress of this wallet for each swap (never the main address).
    auto walletChoice = [this](const Asset &asset) {
        return asset == xmr() ? QString("New XMR subaddress (Biscuit)")
                              : QString("My %1 wallet (Biscuit)").arg(asset.ticker.toUpper());
    };
    if (to.isValid()) {
        ui->combo_receiveMode->setItemText(ModeNewWalletAddress, walletChoice(to));
    }
    if (from.isValid()) {
        ui->combo_refundMode->setItemText(ModeNewWalletAddress, walletChoice(from));
    }
    const bool receiveInWallet = ui->combo_receiveMode->currentIndex() == ModeNewWalletAddress;
    const bool refundInWallet = ui->combo_refundMode->currentIndex() == ModeNewWalletAddress;
    ui->line_receive->setVisible(!receiveInWallet);
    ui->line_refund->setVisible(!refundInWallet);

    if (to.isValid()) {
        ui->line_receive->setPlaceholderText(QString("%1 address").arg(to.displayName()));
    }
    if (from.isValid()) {
        ui->line_refund->setPlaceholderText(sendXmr ? QString("XMR address")
                                                    : QString("%1 address, used only if the swap fails (optional)").arg(from.displayName()));
    }
    clearOffers();
}

void SwapWidget::clearOffers() {
    m_quotes.clear();
    ui->tree_offers->clear();
    ui->label_estimate->clear();
    ui->btn_create->setEnabled(false);
}

void SwapWidget::setBusy(bool busy, const QString &status) {
    ui->btn_offers->setEnabled(!busy);
    ui->btn_create->setEnabled(!busy && !ui->tree_offers->selectedItems().isEmpty());
    ui->label_status->setText(status);
}

// ------------------------------------------------------------------- offers

void SwapWidget::onGetOffers() {
    const Asset from = assetOf(ui->combo_from);
    const Asset to = assetOf(ui->combo_to);
    if (!from.isValid() || !to.isValid()) {
        return;
    }
    const QString amountText = ui->line_amount->text().trimmed();
    if (!amount::isValid(amountText) || amount::isZero(amountText)) {
        Utils::showError(this, "Invalid amount", "Enter a positive amount, with a dot as decimal separator.");
        return;
    }
    if ((fixedRate() ? receivesXmr() : sendsXmr()) && !amount::toAtomic(amountText, moneroDecimals)) {
        Utils::showError(this, "Invalid amount", "Monero amounts have at most 12 decimals.");
        return;
    }

    QuoteRequest request;
    request.from = from;
    request.to = to;
    request.rateType = fixedRate() ? RateType::Fixed : RateType::Floating;
    (fixedRate() ? request.amountTo : request.amountFrom) = amount::normalize(amountText);
    request.minKycRating = noKyc;

    clearOffers();
    setBusy(true, "Asking swap partners for offers…");

    m_manager->requestQuotes(request, [this](const QList<Quote> &ranked, const QStringList &errors) {
        m_quotes = ranked;
        ui->tree_offers->clear();
        for (int i = 0; i < ranked.size(); ++i) {
            const Quote &q = ranked.at(i);
            auto *item = new QTreeWidgetItem(ui->tree_offers);
            item->setText(OfferExchange, q.exchange);
            item->setText(OfferGet, q.rateType == RateType::Fixed ? QString("%1 %2").arg(q.amountFrom, q.from.ticker.toUpper())
                                                                  : QString("%1 %2").arg(q.amountTo, q.to.ticker.toUpper()));
            item->setTextAlignment(OfferGet, Qt::AlignRight | Qt::AlignVCenter);
            item->setText(OfferEta, q.etaMinutes ? QString("~%1 min").arg(*q.etaMinutes) : "–");
            const SwapProvider *p = m_manager->provider(q.providerId);
            item->setText(OfferVia, p ? p->displayName() : q.providerId);
            item->setData(OfferExchange, QuoteIndexRole, i);
        }
        for (int c = OfferGet; c < ui->tree_offers->columnCount(); ++c) {
            ui->tree_offers->resizeColumnToContents(c);
        }

        if (ranked.isEmpty()) {
            setBusy(false, errors.isEmpty() ? "No offer for this pair and amount." : errors.join("\n"));
            return;
        }
        // Best offer preselected; the user can pick another one.
        ui->tree_offers->setCurrentItem(ui->tree_offers->topLevelItem(0));
        const Quote &best = ranked.first();
        ui->label_estimate->setText(best.rateType == RateType::Fixed
                                    ? QString("%1 %2").arg(best.amountFrom, best.from.ticker.toUpper())
                                    : QString("≈ %1 %2").arg(best.amountTo, best.to.ticker.toUpper()));
        setBusy(false, ranked.size() == 1 ? "1 offer." : QString("%1 offers, best one selected.").arg(ranked.size()));
    });
}

QString SwapWidget::newWalletAddress(const QString &label) {
    if (!m_wallet->subaddress()->addRow(label)) {
        return {};
    }
    const quint32 account = m_wallet->currentSubaddressAccount();
    const quint32 count = m_wallet->numSubaddresses(account);
    return count > 0 ? m_wallet->address(account, count - 1) : QString();
}

void SwapWidget::onCreateSwap() {
    const auto items = ui->tree_offers->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    const int index = items.first()->data(OfferExchange, QuoteIndexRole).toInt();
    if (index >= 0 && index < m_quotes.size()) {
        createTrade(m_quotes.at(index));
    }
}

QString SwapWidget::coinWalletLabel(const Asset &asset) const {
    using namespace biscuit::coins;
    const CoinParams *params = asset == Asset{"btc", "Mainnet"} ? &bitcoin()
                             : asset == Asset{"ltc", "Mainnet"} ? &litecoin() : nullptr;
    CoinVault *vault = CoinVault::forWallet(m_wallet);
    if (params && vault && vault->wallets(*params).size() > 1) {
        return QString("your %1 wallet \"%2\" in Biscuit").arg(params->ticker, vault->selectedName(*params));
    }
    return QString("your %1 wallet in Biscuit").arg(asset.ticker.toUpper());
}

QString SwapWidget::coinWalletAddress(const Asset &asset) {
    using namespace biscuit::coins;
    const CoinParams *params = asset == Asset{"btc", "Mainnet"} ? &bitcoin()
                             : asset == Asset{"ltc", "Mainnet"} ? &litecoin() : nullptr;
    CoinVault *vault = CoinVault::forWallet(m_wallet);
    if (!params || !vault) {
        return {};
    }
    // Offers setup or asks the password when needed; it then returns false,
    // but the vault may be ready now.
    if (!m_coins->ensureReady(this) && !vault->isUnlocked()) {
        return {};
    }
    const QString address = vault->wallet(*params)->receiveAddress();
    if (address.isEmpty()) {
        Utils::showError(this, "Address not ready", QString("Your %1 wallet is not ready yet. Try again in a moment.").arg(params->name));
    }
    return address;
}

void SwapWidget::validateTypedAddresses(QList<QPair<Asset, QString>> addresses, std::function<void()> done) {
    if (addresses.isEmpty()) {
        done();
        return;
    }
    const auto [asset, address] = addresses.takeFirst();
    setBusy(true, "Checking address…");
    m_manager->validateAddress(asset, address, [this, asset, addresses, done](std::optional<bool> valid, const QString &error) {
        setBusy(false);
        if (!valid.has_value()) {
            Utils::showError(this, "Unable to check address", error);
            return;
        }
        if (!*valid) {
            Utils::showError(this, "Invalid address", QString("This is not a valid %1 address.").arg(asset.displayName()));
            return;
        }
        validateTypedAddresses(addresses, done);
    });
}

void SwapWidget::createTrade(const Quote &quote) {
    const bool sendXmr = quote.from == xmr();
    const bool receiveXmr = quote.to == xmr();
    const bool receiveInWallet = ui->combo_receiveMode->currentIndex() == ModeNewWalletAddress;
    const bool refundInWallet = ui->combo_refundMode->currentIndex() == ModeNewWalletAddress;
    const QString receiveTyped = ui->line_receive->text().trimmed();
    const QString refundTyped = ui->line_refund->text().trimmed();

    if (!receiveInWallet && receiveTyped.isEmpty()) {
        Utils::showError(this, "Missing address", QString("Enter the %1 address where you want to get the coins.")
                                                          .arg(quote.to.displayName()));
        ui->line_receive->setFocus();
        return;
    }
    if (sendXmr && !refundInWallet && refundTyped.isEmpty()) {
        Utils::showError(this, "Missing refund address", "Enter the XMR address used if the swap fails.");
        ui->line_refund->setFocus();
        return;
    }
    // XMR addresses typed by the user are checked locally, the others by the
    // swap partner (the refund address is optional when receiving XMR).
    QList<QPair<Asset, QString>> external;
    for (const auto &[asset, address] : {qMakePair(quote.to, receiveInWallet ? QString() : receiveTyped),
                                        qMakePair(quote.from, refundInWallet ? QString() : refundTyped)}) {
        if (address.isEmpty()) {
            continue;
        }
        if (asset == xmr()) {
            if (!WalletManager::addressValid(address, constants::networkType)) {
                Utils::showError(this, "Invalid address", "This is not a valid Monero address.");
                return;
            }
        } else {
            external.append({asset, address});
        }
    }

    // Bitcoin and Litecoin addresses of this wallet are known before asking
    // for confirmation (their wallet may need to be set up or unlocked first).
    QString payout = receiveInWallet ? QString() : receiveTyped;
    QString refund = refundInWallet ? QString() : refundTyped;
    if (receiveInWallet && !receiveXmr) {
        payout = coinWalletAddress(quote.to);
        if (payout.isEmpty()) {
            return;
        }
    }
    if (refundInWallet && !sendXmr) {
        refund = coinWalletAddress(quote.from);
        if (refund.isEmpty()) {
            return;
        }
    }

    auto proceed = [this, quote, payout, refund, receiveInWallet, refundInWallet, sendXmr, receiveXmr]() mutable {
        QMessageBox box(this);
        box.setWindowTitle("Create swap");
        box.setIcon(QMessageBox::Question);
        box.setText(QString(quote.rateType == RateType::Fixed ? "Swap %1 %2 for exactly %3 %4 (fixed rate)?"
                                                              : "Swap %1 %2 for about %3 %4?")
                    .arg(quote.amountFrom, quote.from.ticker.toUpper(), quote.amountTo, quote.to.ticker.toUpper()));
        box.setInformativeText(QString("Exchange: %1 (no KYC)\nYou get the coins at: %2")
                               .arg(quote.exchange,
                                    receiveInWallet ? (receiveXmr ? QString("a new subaddress of your XMR wallet")
                                                               : coinWalletLabel(quote.to))
                                                    : payout));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) {
            return;
        }

        // A Monero subaddress is only created once the swap is confirmed.
        if ((receiveInWallet && receiveXmr) || (refundInWallet && sendXmr)) {
            const QString walletAddress = newWalletAddress("Swap");
            if (walletAddress.isEmpty()) {
                Utils::showError(this, "Unable to create a wallet address", m_wallet->subaddress()->getError());
                return;
            }
            (receiveXmr ? payout : refund) = walletAddress;
        }

        TradeRequest request;
        request.quote = quote;
        request.payoutAddress = payout;
        request.refundAddress = refund;

        setBusy(true, "Creating swap…");
        m_manager->createTrade(request, [this](const std::optional<Trade> &trade, const QString &error) {
            setBusy(false);
            if (!trade) {
                Utils::showError(this, "Unable to create swap", error);
                return;
            }
            onClear();
            showTrade(trade->providerId, trade->tradeId);
        });
    };

    validateTypedAddresses(external, proceed);
}

// ------------------------------------------------------------------ history

void SwapWidget::refreshTrades() {
    ui->tree_trades->clear();
    const QList<Trade> trades = m_manager->trades();
    for (const Trade &t : trades) {
        auto *item = new QTreeWidgetItem(ui->tree_trades);
        item->setText(TradeDate, t.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
        item->setText(TradePair, QString("%1 → %2").arg(t.from.ticker.toUpper(), t.to.ticker.toUpper()));
        item->setText(TradeSent, t.amountFrom);
        item->setTextAlignment(TradeSent, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(TradeReceived, t.amountTo);
        item->setTextAlignment(TradeReceived, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(TradeStatusCol, tradeStatusDescription(t.status));
        if (needsSupport(t.status)) {
            item->setIcon(TradeStatusCol, icons()->icon("warning.png"));
        }
        item->setText(TradeExchange, t.providerId == demo::providerId ? t.exchange + " (demo)" : t.exchange);
        item->setData(TradeDate, ProviderRole, t.providerId);
        item->setData(TradeDate, TradeIdRole, t.tradeId);
    }
    for (int c = 0; c < ui->tree_trades->columnCount(); ++c) {
        if (c != TradeStatusCol) {
            ui->tree_trades->resizeColumnToContents(c);
        }
    }
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_history),
                              trades.isEmpty() ? QString("History") : QString("History (%1)").arg(trades.size()));
}

void SwapWidget::onShowTradeDetails() {
    const auto items = ui->tree_trades->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    showTrade(items.first()->data(TradeDate, ProviderRole).toString(),
              items.first()->data(TradeDate, TradeIdRole).toString());
}

void SwapWidget::showTrade(const QString &providerId, const QString &tradeId) {
    auto *dialog = new SwapTradeDialog(m_manager, providerId, tradeId, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SwapTradeDialog::sendDepositRequested, this, &SwapWidget::sendDeposit);
    dialog->show();
}

void SwapWidget::sendDeposit(const Trade &trade) {
    if (demo::isDemoAddress(trade.depositAddress)) {
        Utils::showInfo(this, "Demo swap", "This is a simulated swap: there is nothing to send.");
        return;
    }
    if (!WalletManager::addressValid(trade.depositAddress, constants::networkType)) {
        Utils::showError(this, "Invalid deposit address", "The exchange returned an address that is not a valid Monero address.");
        return;
    }
    const auto atomic = amount::toAtomic(trade.amountFrom, moneroDecimals);
    if (!atomic || *atomic == 0) {
        Utils::showError(this, "Invalid amount", "The swap amount cannot be sent from this wallet.");
        return;
    }

    m_pendingDepositProvider = trade.providerId;
    m_pendingDepositTrade = trade.tradeId;
    m_pendingDepositDescription = QString("Swap %1 → %2 (%3 %4)")
            .arg(trade.from.ticker.toUpper(), trade.to.ticker.toUpper(), trade.exchange, trade.tradeId);

    // The main window shows the usual confirmation dialog: nothing is sent
    // without the user's explicit approval.
    m_wallet->createTransaction(trade.depositAddress, *atomic, m_pendingDepositDescription, false);
}
