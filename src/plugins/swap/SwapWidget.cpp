// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapWidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Amount.h"
#include "DemoSwap.h"
#include "SwapTradeDialog.h"
#include "constants.h"
#include "libwalletqt/Subaddress.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"
#include "utils/Utils.h"

using namespace biscuit::swap;

namespace {
    enum OfferColumn { OfferExchange = 0, OfferReceive, OfferKyc, OfferEta, OfferProvider };
    enum TradeColumn { TradeDate = 0, TradePair, TradeSend, TradeReceive, TradeStatusCol, TradeExchange };

    constexpr int QuoteIndexRole = Qt::UserRole;
    constexpr int ProviderRole = Qt::UserRole;
    constexpr int TradeIdRole = Qt::UserRole + 1;

    constexpr int moneroDecimals = 12;

    const char transparencyNote[] =
            "Biscuit may earn a commission from swap partners. It never affects the ranking: "
            "offers are sorted only by the amount you receive, then by speed. You pay the same "
            "price as on the partner's website.";
}

SwapWidget::SwapWidget(Wallet *wallet, QWidget *parent)
    : QWidget(parent)
    , m_wallet(wallet)
    , m_manager(new SwapManager(wallet, this))
{
    auto *layout = new QVBoxLayout(this);

    if (m_manager->demoMode()) {
        auto *demo = new QLabel("<b>Demo mode</b>: this build has no swap partner key. Offers are simulated, "
                                "no real exchange is contacted and no funds can be sent.", this);
        demo->setWordWrap(true);
        demo->setFrameShape(QFrame::StyledPanel);
        demo->setStyleSheet("QLabel { background-color: #fff3cd; color: #664d03; padding: 4px; }");
        layout->addWidget(demo);
    }

    // Swap partners work on mainnet only: a stagenet or testnet address would
    // lose the funds. The offline demo stays available for development.
    const bool mainnet = constants::networkType == NetworkType::MAINNET;
    if (!mainnet && !m_manager->demoMode()) {
        auto *warning = new QLabel("Swaps are only available on mainnet.", this);
        layout->addWidget(warning);
        layout->addStretch();
        return;
    }

    // [New swap]
    auto *newBox = new QGroupBox("New swap", this);
    auto *form = new QFormLayout(newBox);

    m_direction = new QComboBox(newBox);
    m_direction->addItem("Send XMR from this wallet, receive another coin", SendXmr);
    m_direction->addItem("Send another coin, receive XMR in this wallet", ReceiveXmr);
    form->addRow("Direction:", m_direction);

    m_coin = new QComboBox(newBox);
    m_coin->setMinimumContentsLength(18);
    form->addRow("Other coin:", m_coin);

    auto *amountRow = new QHBoxLayout;
    m_amount = new QLineEdit(newBox);
    m_amount->setPlaceholderText("0.0");
    m_amountUnit = new QLabel(newBox);
    amountRow->addWidget(m_amount);
    amountRow->addWidget(m_amountUnit);
    form->addRow("You send:", amountRow);

    m_payoutLabel = new QLabel(newBox);
    m_payout = new QLineEdit(newBox);
    form->addRow(m_payoutLabel, m_payout);

    m_refundLabel = new QLabel(newBox);
    m_refund = new QLineEdit(newBox);
    form->addRow(m_refundLabel, m_refund);

    m_rateType = new QComboBox(newBox);
    m_rateType->addItem("Floating (amount received may vary slightly)", static_cast<int>(RateType::Floating));
    m_rateType->addItem("Fixed (amount received is guaranteed)", static_cast<int>(RateType::Fixed));
    form->addRow("Rate:", m_rateType);

    m_minKyc = new QComboBox(newBox);
    for (KycRating r : {KycRating::A, KycRating::B, KycRating::C, KycRating::D}) {
        m_minKyc->addItem(QString("%1 or better — %2").arg(kycRatingToString(r), kycRatingDescription(r)), static_cast<int>(r));
    }
    m_minKyc->setCurrentIndex(static_cast<int>(m_manager->minKycRating()));
    form->addRow("KYC rating:", m_minKyc);

    m_btnOffers = new QPushButton("Get offers", newBox);
    auto *offersRow = new QHBoxLayout;
    offersRow->addStretch();
    offersRow->addWidget(m_btnOffers);
    form->addRow(offersRow);
    layout->addWidget(newBox);

    // [Offers]
    auto *offersBox = new QGroupBox("Offers", this);
    auto *offersLayout = new QVBoxLayout(offersBox);
    m_offers = new QTreeWidget(offersBox);
    m_offers->setRootIsDecorated(false);
    m_offers->setHeaderLabels({"Exchange", "You receive", "KYC", "ETA", "Via"});
    m_offers->header()->setSectionResizeMode(OfferExchange, QHeaderView::Stretch);
    offersLayout->addWidget(m_offers);

    auto *note = new QLabel(transparencyNote, offersBox);
    note->setWordWrap(true);
    offersLayout->addWidget(note);

    auto *createRow = new QHBoxLayout;
    m_status = new QLabel(offersBox);
    m_btnCreate = new QPushButton("Create swap", offersBox);
    m_btnCreate->setEnabled(false);
    createRow->addWidget(m_status, 1);
    createRow->addWidget(m_btnCreate);
    offersLayout->addLayout(createRow);
    layout->addWidget(offersBox, 1);

    // [History]
    auto *tradesBox = new QGroupBox("My swaps", this);
    auto *tradesLayout = new QVBoxLayout(tradesBox);
    m_trades = new QTreeWidget(tradesBox);
    m_trades->setRootIsDecorated(false);
    m_trades->setHeaderLabels({"Date", "Pair", "Send", "Receive", "Status", "Exchange"});
    m_trades->header()->setSectionResizeMode(TradeStatusCol, QHeaderView::Stretch);
    tradesLayout->addWidget(m_trades);
    auto *hint = new QLabel("Double-click a swap to see its deposit address, status and support details. "
                            "This list is stored encrypted in your wallet file.", tradesBox);
    hint->setWordWrap(true);
    tradesLayout->addWidget(hint);
    layout->addWidget(tradesBox, 1);

    connect(m_direction, &QComboBox::currentIndexChanged, this, &SwapWidget::onDirectionChanged);
    connect(m_coin, &QComboBox::currentIndexChanged, this, &SwapWidget::onDirectionChanged);
    connect(m_btnOffers, &QPushButton::clicked, this, &SwapWidget::onGetOffers);
    connect(m_btnCreate, &QPushButton::clicked, this, &SwapWidget::onCreateSwap);
    connect(m_offers, &QTreeWidget::itemSelectionChanged, this, [this] {
        m_btnCreate->setEnabled(!m_offers->selectedItems().isEmpty());
    });
    connect(m_minKyc, &QComboBox::currentIndexChanged, this, [this] {
        m_manager->setMinKycRating(static_cast<KycRating>(m_minKyc->currentData().toInt()));
    });
    connect(m_trades, &QTreeWidget::itemDoubleClicked, this, &SwapWidget::onTradeActivated);
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

    loadAssets();
    onDirectionChanged();
    refreshTrades();
}

SwapWidget::Direction SwapWidget::direction() const {
    return static_cast<Direction>(m_direction->currentData().toInt());
}

Asset SwapWidget::xmr() const {
    return {"xmr", "Mainnet"};
}

Asset SwapWidget::selectedCoin() const {
    const QStringList parts = m_coin->currentData().toStringList();
    if (parts.size() != 2) {
        return {};
    }
    return {parts.at(0), parts.at(1)};
}

void SwapWidget::loadAssets() {
    const auto providers = m_manager->providers();
    if (providers.isEmpty()) {
        m_status->setText("All swap providers are disabled in the settings.");
        return;
    }
    // Phase 1: one provider. With several, the lists will be merged.
    providers.first()->supportedAssets([this](const QList<AssetInfo> &assets, const QString &error) {
        if (!error.isEmpty()) {
            m_status->setText("Unable to load coins: " + error);
            return;
        }
        m_coin->clear();
        for (const AssetInfo &info : assets) {
            if (info.asset == xmr()) {
                continue;
            }
            const QString label = info.name.isEmpty() ? info.asset.displayName()
                                                      : QString("%1 — %2").arg(info.asset.displayName(), info.name);
            m_coin->addItem(label, QStringList{info.asset.ticker, info.asset.network});
        }
    });
}

void SwapWidget::onDirectionChanged() {
    const Asset coin = selectedCoin();
    const QString coinName = coin.isValid() ? coin.displayName() : "coin";

    m_offers->clear();
    m_quotes.clear();
    m_btnCreate->setEnabled(false);

    if (direction() == SendXmr) {
        m_amountUnit->setText("XMR");
        m_payoutLabel->setText(QString("Receive %1 at:").arg(coinName));
        m_payout->setReadOnly(false);
        m_payout->clear();
        m_payout->setPlaceholderText(QString("Your %1 address").arg(coinName));
        m_refundLabel->setText("Refund XMR to:");
        m_refund->setReadOnly(true);
        m_refund->clear();
        m_refund->setPlaceholderText("A new address of this wallet");
    } else {
        m_amountUnit->setText(coinName);
        m_payoutLabel->setText("Receive XMR at:");
        m_payout->setReadOnly(true);
        m_payout->clear();
        m_payout->setPlaceholderText("A new address of this wallet");
        m_refundLabel->setText(QString("Refund %1 to:").arg(coinName));
        m_refund->setReadOnly(false);
        m_refund->clear();
        m_refund->setPlaceholderText(QString("Your %1 address, used if the swap fails").arg(coinName));
    }
}

void SwapWidget::setBusy(bool busy, const QString &status) {
    m_btnOffers->setEnabled(!busy);
    m_btnCreate->setEnabled(!busy && !m_offers->selectedItems().isEmpty());
    m_status->setText(status);
}

void SwapWidget::onGetOffers() {
    const Asset coin = selectedCoin();
    if (!coin.isValid()) {
        Utils::showError(this, "No coin selected", "Choose the coin to swap with.");
        return;
    }
    const QString amountText = m_amount->text().trimmed();
    if (!amount::isValid(amountText) || amount::isZero(amountText)) {
        Utils::showError(this, "Invalid amount", "Enter a positive amount, using a dot as decimal separator.");
        return;
    }
    if (direction() == SendXmr && !amount::toAtomic(amountText, moneroDecimals)) {
        Utils::showError(this, "Invalid amount", "Monero amounts have at most 12 decimals.");
        return;
    }

    QuoteRequest request;
    request.from = direction() == SendXmr ? xmr() : coin;
    request.to = direction() == SendXmr ? coin : xmr();
    request.amountFrom = amount::normalize(amountText);
    request.rateType = static_cast<RateType>(m_rateType->currentData().toInt());
    request.minKycRating = static_cast<KycRating>(m_minKyc->currentData().toInt());

    m_offers->clear();
    m_quotes.clear();
    setBusy(true, "Asking swap partners for offers…");

    m_manager->requestQuotes(request, [this, request](const QList<Quote> &ranked, const QStringList &errors) {
        m_quotes = ranked;
        m_offers->clear();
        for (int i = 0; i < ranked.size(); ++i) {
            const Quote &q = ranked.at(i);
            auto *item = new QTreeWidgetItem(m_offers);
            item->setText(OfferExchange, q.exchange);
            item->setText(OfferReceive, QString("%1 %2").arg(q.amountTo, q.to.ticker.toUpper()));
            item->setText(OfferKyc, kycRatingToString(q.kycRating));
            item->setToolTip(OfferKyc, kycRatingDescription(q.kycRating));
            item->setText(OfferEta, q.etaMinutes ? QString("~%1 min").arg(*q.etaMinutes) : "?");
            const SwapProvider *p = m_manager->provider(q.providerId);
            item->setText(OfferProvider, p ? p->displayName() : q.providerId);
            item->setData(OfferExchange, QuoteIndexRole, i);
        }
        for (int c = 0; c < m_offers->columnCount(); ++c) {
            m_offers->resizeColumnToContents(c);
        }

        if (ranked.isEmpty()) {
            setBusy(false, errors.isEmpty() ? "No offer for this pair and amount." : errors.join("\n"));
            return;
        }
        // Best offer preselected; the user can pick another one.
        m_offers->setCurrentItem(m_offers->topLevelItem(0));
        setBusy(false, QString("%1 offer(s). Best offer selected.").arg(ranked.size()));
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
    const auto items = m_offers->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    const int index = items.first()->data(OfferExchange, QuoteIndexRole).toInt();
    if (index < 0 || index >= m_quotes.size()) {
        return;
    }
    createTrade(m_quotes.at(index));
}

void SwapWidget::createTrade(const Quote &quote) {
    const bool sendXmr = direction() == SendXmr;
    const QString external = (sendXmr ? m_payout : m_refund)->text().trimmed();

    if (sendXmr && external.isEmpty()) {
        Utils::showError(this, "Missing address", QString("Enter the %1 address where you want to receive the coins.")
                                                          .arg(quote.to.displayName()));
        return;
    }

    auto proceed = [this, quote, sendXmr, external] {
        const QString summary = QString(
                "You send: %1 %2\n"
                "You receive (estimated): %3 %4\n"
                "Exchange: %5 (KYC rating %6)\n\n"
                "%7")
                .arg(quote.amountFrom, quote.from.ticker.toUpper(), quote.amountTo, quote.to.ticker.toUpper(),
                     quote.exchange, kycRatingToString(quote.kycRating),
                     sendXmr ? QString("Coins will be sent to:\n%1").arg(external)
                             : QString("XMR will be received on a new address of this wallet."));

        if (QMessageBox::question(this, "Create swap?", summary) != QMessageBox::Yes) {
            return;
        }

        const QString walletAddress = newWalletAddress("Swap");
        if (walletAddress.isEmpty()) {
            Utils::showError(this, "Unable to create a wallet address", m_wallet->subaddress()->getError());
            return;
        }

        TradeRequest request;
        request.quote = quote;
        request.payoutAddress = sendXmr ? external : walletAddress;
        request.refundAddress = sendXmr ? walletAddress : external;

        setBusy(true, "Creating swap…");
        m_manager->createTrade(request, [this](const std::optional<Trade> &trade, const QString &error) {
            setBusy(false);
            if (!trade) {
                Utils::showError(this, "Unable to create swap", error);
                return;
            }
            m_offers->clear();
            m_quotes.clear();
            showTrade(trade->providerId, trade->tradeId);
        });
    };

    // Validate the external address with the partner first (the refund
    // address is optional when receiving XMR).
    if (external.isEmpty()) {
        proceed();
        return;
    }
    const Asset externalAsset = sendXmr ? quote.to : quote.from;
    setBusy(true, "Checking address…");
    m_manager->validateAddress(externalAsset, external, [this, proceed, externalAsset](std::optional<bool> valid, const QString &error) {
        setBusy(false);
        if (valid.has_value() && !*valid) {
            Utils::showError(this, "Invalid address", QString("This is not a valid %1 address.").arg(externalAsset.displayName()));
            return;
        }
        if (!valid.has_value()) {
            Utils::showError(this, "Unable to check address", error);
            return;
        }
        proceed();
    });
}

void SwapWidget::refreshTrades() {
    m_trades->clear();
    for (const Trade &t : m_manager->trades()) {
        auto *item = new QTreeWidgetItem(m_trades);
        item->setText(TradeDate, t.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
        item->setText(TradePair, QString("%1 → %2").arg(t.from.ticker.toUpper(), t.to.ticker.toUpper()));
        item->setText(TradeSend, t.amountFrom);
        item->setText(TradeReceive, t.amountTo);
        item->setText(TradeStatusCol, tradeStatusDescription(t.status));
        item->setText(TradeExchange, t.providerId == demo::providerId ? t.exchange + " (demo)" : t.exchange);
        item->setData(TradeDate, ProviderRole, t.providerId);
        item->setData(TradeDate, TradeIdRole, t.tradeId);
    }
    for (int c = 0; c < m_trades->columnCount(); ++c) {
        m_trades->resizeColumnToContents(c);
    }
}

void SwapWidget::onTradeActivated() {
    const auto items = m_trades->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    showTrade(items.first()->data(TradeDate, ProviderRole).toString(),
              items.first()->data(TradeDate, TradeIdRole).toString());
}

void SwapWidget::showTrade(const QString &providerId, const QString &tradeId) {
    auto *dialog = new SwapTradeDialog(m_manager, providerId, tradeId, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SwapTradeDialog::sendDepositRequested, this, [this](const Trade &trade) {
        sendDeposit(trade);
    });
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
