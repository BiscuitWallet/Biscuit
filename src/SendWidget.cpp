// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "SendWidget.h"
#include "ui_SendWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "coins/CoinPicker.h"
#include "coins/CoinSendController.h"
#include "coins/CoinWalletBar.h"
#include "coins/core/CoinParams.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"

#include "ColorScheme.h"
#include "constants.h"
#include "utils/AppData.h"
#include "utils/config.h"
#include "Icons.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"

#if defined(WITH_SCANNER)
#include "wizard/offline_tx_signing/OfflineTxSigningWizard.h"
#include "qrcode/scanner/QrCodeScanDialog.h"
#include <QMediaDevices>
#endif

SendWidget::SendWidget(Wallet *wallet, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SendWidget)
    , m_wallet(wallet)
{
    ui->setupUi(this);

    QString amount_rx = R"(^\d{0,8}[\.,]\d{0,12}|(all)$)";
    QRegularExpression rx;
    rx.setPattern(amount_rx);
    ui->lineAmount->setValidator(new QRegularExpressionValidator(rx, this));

    connect(m_wallet, &Wallet::initiateTransaction, this, &SendWidget::disableSendButton);
    connect(m_wallet, &Wallet::transactionCreated, this, &SendWidget::enableSendButton);
    connect(m_wallet, &Wallet::beginCommitTransaction, this, &SendWidget::disableSendButton);
    connect(m_wallet, &Wallet::transactionCommitted, this, &SendWidget::enableSendButton);

    connect(WalletManager::instance(), &WalletManager::openAliasResolved, this, &SendWidget::onOpenAliasResolved);

    connect(ui->btnScan, &QPushButton::clicked, this, &SendWidget::scanClicked);
    connect(ui->btnSend, &QPushButton::clicked, this, &SendWidget::sendClicked);
    connect(ui->btnClear, &QPushButton::clicked, this, &SendWidget::clearClicked);
    connect(ui->btnMax, &QPushButton::clicked, this, &SendWidget::btnMaxClicked);
    connect(ui->comboCurrencySelection, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SendWidget::currencyComboChanged);
    connect(ui->lineAmount, &QLineEdit::textChanged, this, &SendWidget::amountEdited);
    connect(ui->lineAddress, &QPlainTextEdit::textChanged, this, &SendWidget::addressEdited);
    connect(ui->btn_openAlias, &QPushButton::clicked, this, &SendWidget::aliasClicked);
    connect(ui->lineAddress, &PayToEdit::dataPasted, this, &SendWidget::onDataFromQR);
    ui->label_conversionAmount->setText("");
    ui->label_conversionAmount->hide();
    ui->btn_openAlias->hide();

    ui->label_PayTo->setHelpText("Recipient of the funds",
                                 "You may enter a Monero address, or an alias (email-like address that forwards to a Monero address)",
                                 "send_transaction");
    ui->label_Description->setHelpText("Description of the transaction (optional)",
                                       "The description is not sent to the recipient of the funds. It is stored in your wallet cache, "
                                       "and displayed in the 'History' tab.",
                                       "send_transaction");
    ui->label_Amount->setHelpText("Amount to be sent","This is the exact amount the recipient will receive. "
                                  "In addition to this amount a transaction fee will be subtracted from your balance. "
                                  "You will be able to review the transaction fee before the transaction is broadcast.\n\n"
                                  "To send all your balance, click the Max button to the right.","send_transaction");

    ui->lineAddress->setNetType(constants::networkType);
    this->setupComboBox();

    this->setManualFeeSelectionEnabled(conf()->get(Config::manualFeeTierSelection).toBool());
    this->setSubtractFeeFromAmountEnabled(conf()->get(Config::subtractFeeFromAmount).toBool());

    // Biscuit: one Send tab for every coin. A Bitcoin or Litecoin address is
    // detected when pasted, and the amount, fee and send path adapt to it.
    ui->label_PayTo->setHelpText("Recipient of the funds",
                                 "You may enter a Monero, Bitcoin or Litecoin address, a payment URI, or an alias. "
                                 "Biscuit detects the coin from the address.",
                                 "send_transaction");
    m_coinSend = new biscuit::coins::CoinSendController(m_wallet, this);
    connect(m_coinSend, &biscuit::coins::CoinSendController::sent, this, &SendWidget::clearFields);

    // The coin to send, as in Receive; for Bitcoin and Litecoin, the wallet it
    // is sent from. Pasting an address of another coin switches to that coin.
    m_coinPicker = new biscuit::coins::CoinPicker(this);
    biscuit::coins::addWalletCoins(m_coinPicker, true);
    ui->formLayout->insertRow(0, m_coinPicker);
    auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
    for (const auto *params : {&biscuit::coins::bitcoin(), &biscuit::coins::litecoin(), &biscuit::coins::ethereum()}) {
        auto *bar = new biscuit::coins::CoinWalletBar(vault, *params, this);
        ui->formLayout->insertRow(1 + m_walletBars.size(), bar);
        m_walletBars << bar;
    }
    // Monero: its balance on the right, like the Bitcoin/Litecoin wallets.
    m_xmrBalanceRow = new QWidget(this);
    auto *xmrLayout = new QHBoxLayout(m_xmrBalanceRow);
    xmrLayout->setContentsMargins(0, 0, 0, 0);
    xmrLayout->addStretch();
    m_xmrBalance = new QLabel(m_xmrBalanceRow);
    m_xmrBalance->setTextFormat(Qt::RichText);
    xmrLayout->addWidget(m_xmrBalance);
    ui->formLayout->insertRow(1 + m_walletBars.size(), m_xmrBalanceRow);
    connect(m_wallet, &Wallet::balanceUpdated, this, &SendWidget::updateXmrBalance);
    updateXmrBalance();
    connect(m_coinPicker, &biscuit::coins::CoinPicker::currentIndexChanged, this, &SendWidget::updateCoinMode);
    for (auto signal : {&biscuit::coins::CoinVault::unlocked, &biscuit::coins::CoinVault::locked,
                        &biscuit::coins::CoinVault::walletsChanged}) {
        connect(vault, signal, this, &SendWidget::updateCoinMode);
    }
    biscuit::coins::showWalletCoins(m_coinPicker, vault, true);

    m_coinHint = new QLabel(this);
    m_coinHint->setStyleSheet("color: gray;");
    int row = 0;
    QFormLayout::ItemRole role;
    ui->formLayout->getWidgetPosition(ui->label_PayTo, &row, &role);
    ui->formLayout->insertRow(row + 1, QString(), m_coinHint);

    // Coin control: the coins chosen in the Coins tab, until sent or cleared.
    m_coinSelectionRow = new QWidget(this);
    auto *selectionLayout = new QHBoxLayout(m_coinSelectionRow);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    m_coinSelection = new QLabel(m_coinSelectionRow);
    auto *clearSelection = new QPushButton("Use any coins", m_coinSelectionRow);
    clearSelection->setAutoDefault(false);
    connect(clearSelection, &QPushButton::clicked, this, [this] {
        if (m_coin) {
            biscuit::coins::CoinVault::forWallet(m_wallet)->setCoinSelection(*m_coin, {});
        }
    });
    selectionLayout->addWidget(m_coinSelection);
    selectionLayout->addWidget(clearSelection);
    selectionLayout->addStretch();
    ui->formLayout->insertRow(row + 2, QString(), m_coinSelectionRow);
    connect(vault, &biscuit::coins::CoinVault::coinSelectionChanged, this, &SendWidget::updateCoinMode);

    m_coinUnit = new QLabel(this);
    ui->horizontalLayout_2->addWidget(m_coinUnit);
    // Ethereum fees follow the node: the estimate follows them, and its value
    // follows the preferred currency.
    connect(vault, &biscuit::coins::CoinVault::walletUpdated, this, &SendWidget::updateCoinFeeLabel);
    connect(conf(), &Config::changed, this, [this](Config::ConfigKey key) {
        if (key == Config::preferredFiatCurrency) {
            this->updateCoinFeeLabel();
            this->updateConversionLabel();
        }
    });

    m_coinFeeTitle = new QLabel("Network fee", this);
    m_coinFee = new QComboBox(this);
    for (const auto &level : biscuit::coins::feeLevels(biscuit::coins::bitcoin())) {
        m_coinFee->addItem(level.label, level.targetBlocks);
    }
    m_coinFee->setCurrentIndex(1);
    m_coinFeeRate = new QLabel(this);
    m_coinFeeRate->setTextFormat(Qt::RichText);   // the "not enough ETH" line
    m_coinFeeRate->setTextInteractionFlags(Qt::TextSelectableByMouse);   // to copy the fee
    m_coinFeeRate->setWordWrap(true);   // the gas line can be long when the network is busy
    m_coinFeeRate->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);   // the whole width, not a column
    auto *feeRow = new QHBoxLayout;
    feeRow->setSpacing(12);
    feeRow->addWidget(m_coinFee);
    feeRow->addWidget(m_coinFeeRate, 1);   // the text takes the rest of the row
    ui->formLayout->getWidgetPosition(ui->label_Amount, &row, &role);
    ui->formLayout->insertRow(row + 1, m_coinFeeTitle, feeRow);
    connect(m_coinFee, &QComboBox::currentIndexChanged, this, &SendWidget::updateCoinFeeLabel);

    this->updateCoinMode();
}

void SendWidget::showCoin(int coinIndex) {
    m_coinPicker->setCurrentIndex(coinIndex);
    this->updateCoinMode();
}

void SendWidget::updateCoinMode() {
    const auto destination = biscuit::coins::detectCoinDestination(ui->lineAddress->text());
    // A pasted address selects its coin (Bitcoin, Litecoin, Ethereum or Monero).
    // An Ethereum address on the USDT or USDC tab stays there: same addresses.
    int detected = -1;
    const auto *current = biscuit::coins::coinOfTicker(m_coinPicker->ticker(m_coinPicker->currentIndex()));
    if (destination && current && destination->params == current) {
        detected = m_coinPicker->currentIndex();
    } else if (destination) {
        detected = m_coinPicker->indexOf(destination->params->ticker);
    } else if (WalletManager::addressValid(ui->lineAddress->text().trimmed(), constants::networkType)) {
        detected = 0;
    }
    // Not a coin of this wallet: stays on Monero, which refuses the address.
    if (detected >= 0 && m_coinPicker->currentIndex() != detected && m_coinPicker->isCoinVisible(detected)) {
        QSignalBlocker blocker(m_coinPicker);
        m_coinPicker->setCurrentIndex(detected);
    }
    const int coinIndex = m_coinPicker->currentIndex();
    const QString ticker = m_coinPicker->ticker(coinIndex);
    m_coin = biscuit::coins::coinOfTicker(ticker);
    const bool coinMode = m_coin != nullptr;
    const bool ethereum = coinMode && m_coin->ethereum;
    m_asset = ethereum ? ticker : QString();

    const QList<const biscuit::coins::CoinParams *> barCoins{&biscuit::coins::bitcoin(), &biscuit::coins::litecoin(),
                                                             &biscuit::coins::ethereum()};
    for (int i = 0; i < m_walletBars.size(); ++i) {
        // A bar also hides itself while the coin is locked or not added.
        auto *bar = static_cast<biscuit::coins::CoinWalletBar *>(m_walletBars.at(i));
        bar->setActive(m_coin == barCoins.value(i));
        if (ethereum) bar->setAsset(m_asset);
    }
    ui->lineAddress->setPlaceholderText(!m_coin   ? QString("Monero address (4… or 8…)")
                                        : ethereum ? QString("Ethereum address (0x…)")
                                        : QString("%1 address (%2…)").arg(m_coin->name, *m_coin == biscuit::coins::bitcoin() ? "bc1" : "ltc1"));
    ui->formLayout->setRowVisible(m_xmrBalanceRow, !coinMode);

    ui->formLayout->setRowVisible(m_coinHint, coinMode);
    const QStringList chosen = coinMode && !ethereum ? biscuit::coins::CoinVault::forWallet(m_wallet)->coinSelection(*m_coin) : QStringList();
    m_coinSelection->setText(QString("Coin control: only the %1 coin(s) selected in the Coins tab").arg(chosen.size()));
    ui->formLayout->setRowVisible(m_coinSelectionRow, !chosen.isEmpty());
    ui->formLayout->setRowVisible(m_coinFeeTitle, coinMode);
    m_coinFee->setVisible(coinMode && !ethereum);   // Ethereum: one fee, from the node
    m_coinUnit->setVisible(coinMode);
    ui->comboCurrencySelection->setVisible(!coinMode);

    // Monero-only options.
    const bool manualFee = conf()->get(Config::manualFeeTierSelection).toBool();
    ui->label_feeTarget->setVisible(!coinMode && manualFee);
    ui->combo_feePriority->setVisible(!coinMode && manualFee);
    ui->check_subtractFeeFromAmount->setVisible(!coinMode && conf()->get(Config::subtractFeeFromAmount).toBool());

    if (coinMode) {
        const auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
        const auto state = vault->coinState(*m_coin);
        QString hint = state == biscuit::coins::CoinVault::CoinState::Ready
                                ? QString("Sent from your %1 wallet \"%2\"").arg(ethereum ? QString("Ethereum") : m_coin->ticker,
                                                                                   vault->selectedName(*m_coin))
                            : state == biscuit::coins::CoinVault::CoinState::Locked
                                ? QString("Sent from the %1 wallet of Biscuit").arg(m_coin->name)
                                : QString("%1 is not in this wallet yet: add it with + above.").arg(m_coin->name);
        if (ethereum && m_asset != "ETH") {
            hint += QString(" · on Ethereum (ERC-20); the network fee is paid in ETH");
        }
        m_coinHint->setText(hint);
        m_coinUnit->setText(ethereum ? m_asset : m_coin->ticker);
        // Times follow the coin's blocks.
        const auto levels = biscuit::coins::feeLevels(*m_coin);
        for (int i = 0; i < levels.size() && i < m_coinFee->count(); ++i) {
            m_coinFee->setItemText(i, levels[i].label);
        }
        if (destination && !destination->amount.isEmpty() && ui->lineAmount->text().isEmpty()) {
            ui->lineAmount->setText(destination->amount);
        }
        this->updateCoinFeeLabel();
    }
    this->updateConversionLabel();
}

void SendWidget::updateXmrBalance() {
    const quint64 total = m_wallet->balance();
    const quint64 unlocked = m_wallet->unlockedBalance();
    QString text = QString("Balance: <b>%1 XMR</b>").arg(WalletManager::displayAmount(total, false));
    if (total > unlocked) {
        // Coins received recently are spendable after 10 confirmations (~20 min).
        text += QString(" (%1 XMR spendable now)").arg(WalletManager::displayAmount(unlocked, false));
    }
    m_xmrBalance->setText(text);
}

void SendWidget::updateCoinFeeLabel() {
    if (!m_coin) {
        return;
    }
    if (m_coin->ethereum) {
        const auto fee = m_coinSend->ethereumFee(m_asset);
        QString text, tip;
        if (fee) {
            // Money first, the ETH amount short; the exact figures in the tooltip.
            using biscuit::coins::fiatAmount;
            using biscuit::coins::shortAmount;
            const QString about = fiatAmount("ETH", fee->first), most = fiatAmount("ETH", fee->second);
            text = !about.isEmpty() ? QString("≈ %1 (%2 ETH) · at most %3").arg(about, shortAmount(fee->first), most)
                                    : QString("about %1 ETH · at most %2 ETH").arg(shortAmount(fee->first), shortAmount(fee->second));
            tip = QString("About %1 ETH, at most %2 ETH.\nYou usually pay about the first; the second is the most it can "
                          "cost if fees rise before the transaction is in a block.").arg(fee->first, fee->second);
        }
        // How busy Ethereum is: most days of 2026 stay under 1 gwei.
        if (const auto gwei = m_coinSend->ethereumGwei()) {
            const QString value = QString::number(*gwei, 'f', *gwei < 10 ? 2 : 1);
            text += "<br>" + (*gwei >= 6 ? QString("<span style=\"color: #c0392b;\"><b>%1 gwei · very busy</b> · waiting is often cheaper</span>").arg(value)
                              : *gwei >= 2 ? QString("<span style=\"color: #d35400;\">%1 gwei · busy</span>").arg(value)
                                           : QString("<span style=\"color: gray;\">%1 gwei · calm</span>").arg(value));
            tip += QString("\n\nGas price now: %1 gwei. On most days Ethereum stays under 1 gwei; when it is busy, "
                           "fees can be 10 times higher for a few hours.").arg(value);
        }
        m_coinFeeRate->setToolTip(tip);
        // A token with no ETH to pay its fee: said plainly, before trying.
        const auto *eth = biscuit::coins::CoinVault::forWallet(m_wallet)->ethereum();
        if (fee && eth && m_asset != "ETH"
            && eth->balance("ETH") < biscuit::coins::eth::parseAmount(fee->second, biscuit::coins::eth::etherDecimals).value_or(0)) {
            text += QString("<br><span style=\"color: #c0392b;\">This wallet has %1 ETH: not enough for the network fee. "
                            "Receive a little ETH on this address first.</span>")
                    .arg(biscuit::coins::eth::formatAmount(eth->balance("ETH"), biscuit::coins::eth::etherDecimals));
        }
        m_coinFeeRate->setText(text);
        return;
    }
    const auto rate = m_coinSend->feeRate(*m_coin, m_coinFee->currentData().toInt());
    QString text = rate ? QString("%1 sat/vB").arg(*rate, 0, 'f', 1) : QString();
    // When the next blocks have room, every speed costs the same (common on
    // Litecoin): say so, it is not a fault.
    const auto fastest = m_coinSend->feeRate(*m_coin, 2);
    const auto slowest = m_coinSend->feeRate(*m_coin, 24);
    if (rate && fastest && slowest && qFuzzyCompare(*fastest, *slowest)) {
        text += QString(" · same for every speed right now: the %1 blocks are not full").arg(m_coin->name);
    }
    m_coinFeeRate->setText(text);
}

void SendWidget::currencyComboChanged(int index) {
    Q_UNUSED(index)
    QString amount = ui->lineAmount->text();
    if (amount.isEmpty()) {
        return;
    }
    this->amountEdited(amount);
}

void SendWidget::addressEdited() {
    QVector<PartialTxOutput> outputs = ui->lineAddress->getOutputs();

    bool freezeAmounts = !outputs.empty();

    ui->lineAmount->setReadOnly(freezeAmounts);
    ui->lineAmount->setFrame(!freezeAmounts);
    ui->btnMax->setDisabled(freezeAmounts);
    ui->comboCurrencySelection->setDisabled(freezeAmounts);

    if (!outputs.empty()) {
        ui->lineAmount->setText(WalletManager::displayAmount(ui->lineAddress->getTotal(), false));
        ui->comboCurrencySelection->setCurrentIndex(0);
    }

    ui->btn_openAlias->setVisible(ui->lineAddress->isOpenAlias());
    this->updateCoinMode();
}

void SendWidget::amountEdited(const QString &text) {
    Q_UNUSED(text)
    this->updateConversionLabel();
}

void SendWidget::fill(double amount) {
    ui->lineAmount->setText(QString::number(amount));
}

void SendWidget::fill(const QString &address, const QString &description, double amount, bool overrideDescription) {
    ui->lineAddress->setText(address);
    ui->lineAddress->moveCursor(QTextCursor::Start);

    if (overrideDescription || ui->lineDescription->text().isEmpty()) {
      ui->lineDescription->setText(description);
    }

    if (amount > 0)
        ui->lineAmount->setText(QString::number(amount));
    ui->lineAmount->setFocus();

    this->updateConversionLabel();
}

void SendWidget::fillAddress(const QString &address) {
    ui->lineAddress->setText(address);
    ui->lineAddress->moveCursor(QTextCursor::Start);
}

void SendWidget::scanClicked() {
#if defined(WITH_SCANNER)
    if (QMediaDevices::videoInputs().empty()) {
        Utils::showError(this, "Can't open QR scanner", "No available cameras found");
        return;
    }
    this->onDataFromQR(Utils::scanQrCode(this));
#else
    Utils::showError(this, "Can't open QR scanner", "Biscuit was built without webcam QR scanner support");
#endif
}

void SendWidget::sendClicked() {
    // Biscuit: Bitcoin and Litecoin go through their own wallet (the address
    // is checked against the selected coin before anything is sent).
    if (m_coin) {
        const auto destination = biscuit::coins::detectCoinDestination(ui->lineAddress->text());
        const QString address = destination ? destination->address : ui->lineAddress->text().trimmed();
        if (m_coin->ethereum) {
            m_coinSend->sendEthereum(this, m_asset, address, ui->lineAmount->text());
            return;
        }
        m_coinSend->send(this, *m_coin, address, ui->lineAmount->text(), m_coinFee->currentData().toInt());
        return;
    }

    if (!m_wallet->isConnected()) {
        Utils::showError(this, "Unable to create transaction", "Wallet is not connected to a node.",
                         {"Wait for the wallet to automatically connect to a node.", "Go to File -> Settings -> Network -> Node to manually connect to a node."},
                         "nodes");
        return;
    }

    if (!m_wallet->isSynchronized()) {
        Utils::showError(this, "Unable to create transaction", "Wallet is not synchronized", {"Wait for wallet synchronization to complete"}, "synchronization");
        return;
    }

    QString recipient = ui->lineAddress->text().simplified().remove(' ');
    if (recipient.isEmpty()) {
        Utils::showError(this, "Unable to create transaction", "No address was entered", {"Enter an address in the 'Pay to' field."}, "send_transaction");
        return;
    }

    QVector<PartialTxOutput> outputs = ui->lineAddress->getOutputs();
    QVector<PayToLineError> errors = ui->lineAddress->getErrors();
    if (!errors.empty() && ui->lineAddress->isMultiline()) {
        QString errorText;
        for (auto &error: errors) {
            errorText += QString("Line #%1:\n%2\n").arg(QString::number(error.idx + 1), error.error);
        }

        Utils::showError(this, "Unable to create transaction", QString("Invalid address lines found:\n\n%1").arg(errorText), {}, "pay_to_many");
        return;
    }

    bool subtractFeeFromAmount = conf()->get(Config::subtractFeeFromAmount).toBool() && ui->check_subtractFeeFromAmount->isChecked();

    QString description = ui->lineDescription->text();

    if (!outputs.empty()) { // multi destination transaction
        if (outputs.size() > 15) {
            Utils::showError(this, "Unable to create transaction", "Maximum number of outputs (15) exceeded.", {}, "pay_to_many");
            return;
        }

        QVector<QString> addresses;
        QVector<quint64> amounts;
        for (auto &output : outputs) {
            addresses.push_back(output.address);
            amounts.push_back(output.amount);
        }

        QtFuture::connect(m_wallet, &Wallet::preTransactionChecksComplete)
                .then([this, addresses, amounts, description, subtractFeeFromAmount](int feeLevel){
                    m_wallet->createTransactionMultiDest(addresses, amounts, description, feeLevel, subtractFeeFromAmount);
                });

        m_wallet->preTransactionChecks(ui->combo_feePriority->currentIndex());

        return;
    }

    bool sendAll = (ui->lineAmount->text() == "all");
    QString currency = ui->comboCurrencySelection->currentText();
    quint64 amount = this->amount();

    if (amount == 0 && !sendAll) {
        Utils::showError(this, "Unable to create transaction", "No amount was entered", {}, "send_transaction", "Amount field");
        return;
    }

    if (currency != "XMR" && !sendAll) {
        if (!appData()->prices.canConvert(currency, "XMR")) {
            Utils::showError(this, "Unable to create transaction",
                             QString("No exchange rates were received, so the amount in %1 can't be converted to XMR.").arg(currency),
                             {"Enter the amount in XMR instead, or",
                              "Wait for exchange rates to be received, then try again."},
                             "send_transaction", "Amount field");
            return;
        }

        // Convert fiat amount to XMR, but only if we're not sending the entire balance
        amount = WalletManager::amountFromDouble(this->conversionAmount());
    }

    quint64 unlocked_balance = m_wallet->unlockedBalance();
    quint64 total_balance = m_wallet->balance();
    if (total_balance == 0) {
        Utils::showError(this, "Unable to create transaction", "No money to spend");
        return;
    }

    if (unlocked_balance == 0) {
        Utils::showError(this, "Unable to create transaction", QString("No spendable balance.\n\n%1 XMR becomes spendable within 10 blocks (~20 minutes).").arg(WalletManager::displayAmount(total_balance - unlocked_balance)), {"Wait for more balance to unlock.", "Click 'Help' to learn more about how balance works."}, "balance");
        return;
    }

    if (!sendAll && amount > unlocked_balance) {
        Utils::showError(this, "Unable to create transaction", QString("Not enough money to spend.\n\n"
                                                                       "Spendable balance: %1").arg(WalletManager::displayAmount(unlocked_balance)));
        return;
    }

    // TODO: allow using file-only airgapped signing without scanner

    if (m_wallet->keyImageSyncNeeded(amount, sendAll)) {
        #if defined(WITH_SCANNER)
        OfflineTxSigningWizard wizard(this, m_wallet);
        auto r = wizard.exec();
        m_wallet->setForceKeyImageSync(false);

        if (r == QDialog::Rejected) {
            return;
        }
        #else
        Utils::showError(this, "Can't open offline transaction signing wizard", "Biscuit was built without webcam QR scanner support");
        return;
        #endif
    }

    QtFuture::connect(m_wallet, &Wallet::preTransactionChecksComplete)
            .then([this, recipient, amount, description, sendAll, subtractFeeFromAmount](int feeLevel){
                m_wallet->createTransaction(recipient, amount, description, sendAll, feeLevel, subtractFeeFromAmount);
            });

    m_wallet->preTransactionChecks(ui->combo_feePriority->currentIndex());
}

void SendWidget::aliasClicked() {
    ui->btn_openAlias->setEnabled(false);
    auto alias = ui->lineAddress->text();
    WalletManager::instance()->resolveOpenAliasAsync(alias);
}

void SendWidget::clearClicked() {
    ui->lineAddress->clear();
    ui->lineAmount->clear();
    ui->lineDescription->clear();
}

void SendWidget::btnMaxClicked() {
    ui->lineAmount->setText("all");
    this->updateConversionLabel();
}

void SendWidget::updateConversionLabel() {
    if (m_coin) {
        // Display only: fiat value of the Bitcoin/Litecoin/Ethereum amount.
        const double coinAmount = ui->lineAmount->text().replace(',', '.').toDouble();
        const QString fiat = conf()->get(Config::preferredFiatCurrency).toString();
        const QString unit = m_coin->ethereum ? m_asset : m_coin->ticker;
        if (coinAmount > 0 && appData()->prices.canConvert(unit, fiat)) {
            ui->label_conversionAmount->setText(QString("~%1 %2").arg(
                    QString::number(appData()->prices.convert(unit, fiat, coinAmount), 'f', 2), fiat));
            ui->label_conversionAmount->show();
        } else {
            ui->label_conversionAmount->hide();
        }
        return;
    }

    auto amount = this->amountDouble();

    ui->label_conversionAmount->setText("");
    if (amount <= 0) {
        ui->label_conversionAmount->hide();
        return;
    }

    if (conf()->get(Config::disableWebsocket).toBool()) {
        return;
    }

    QString currency = ui->comboCurrencySelection->currentText();
    auto preferredFiatCurrency = conf()->get(Config::preferredFiatCurrency).toString();
    if (!appData()->prices.canConvert(currency, currency != "XMR" ? "XMR" : preferredFiatCurrency)) {
        ui->label_conversionAmount->hide();
        return;
    }

    QString conversionAmountStr = [this, &currency, &preferredFiatCurrency]{
        if (currency != "XMR") {
            return QString("~%1 XMR").arg(QString::number(this->conversionAmount(), 'f'));

        } else {
            double conversionAmount = appData()->prices.convert("XMR", preferredFiatCurrency, this->amountDouble());
            return QString("~%1 %2").arg(QString::number(conversionAmount, 'f', 2), preferredFiatCurrency);
        }
    }();

    ui->label_conversionAmount->setText(conversionAmountStr);
    ui->label_conversionAmount->show();
}

double SendWidget::conversionAmount() {
    QString currency = ui->comboCurrencySelection->currentText();
    return appData()->prices.convert(currency, "XMR", this->amountDouble());
}

quint64 SendWidget::amount() {
    // grab amount from "amount" text box
    QString amount = ui->lineAmount->text();
    if (amount == "all") {
        return 0;
    }

    amount.replace(',', '.');
    if (amount.isEmpty()) {
        return 0;
    }

    return WalletManager::amountFromString(amount);
}

double SendWidget::amountDouble() {
    quint64 amount = this->amount();
    return amount / constants::cdiv;
}

void SendWidget::onOpenAliasResolved(const QString &openAlias, const QString &address, bool dnssecValid) {
    ui->btn_openAlias->setEnabled(true);

    if (address.isEmpty()) {
        Utils::showError(this, "Unable to resolve OpenAlias", "Address empty.");
        return;
    }

    if (!dnssecValid) {
        Utils::showError(this, "Unable to resolve OpenAlias", "Address found, but the DNSSEC signatures could not be verified, so this address may be spoofed.");
        return;
    }

    bool valid = WalletManager::addressValid(address, constants::networkType);
    if (!valid) {
        Utils::showError(this, "Unable to resolve OpenAlias", QString("Address validation failed.\n\nOpenAlias: %1\nAddress: %2").arg(openAlias, address));
        return;
    }

    this->fill(address, openAlias);
    ui->btn_openAlias->hide();
}

void SendWidget::clearFields() {
    ui->lineAddress->clear();
    ui->lineAmount->clear();
    ui->lineDescription->clear();
    ui->label_conversionAmount->clear();
}

void SendWidget::payToMany() {
    ui->lineAddress->payToMany();
}

void SendWidget::disableSendButton() {
    ui->btnSend->setEnabled(false);
}

void SendWidget::enableSendButton() {
    if (m_disallowSending) {
        return;
    }
    ui->btnSend->setEnabled(true);
}

void SendWidget::disallowSending() {
    m_disallowSending = true;
    ui->btnSend->setEnabled(false);
}

void SendWidget::setWebsocketEnabled(bool enabled) {
    this->updateConversionLabel();
    if (enabled) {
        this->setupComboBox();
    } else {
        ui->comboCurrencySelection->clear();
        ui->comboCurrencySelection->insertItem(0, "XMR");
    }
}

void SendWidget::setManualFeeSelectionEnabled(bool enabled) {
    ui->label_feeTarget->setVisible(enabled);
    ui->combo_feePriority->setVisible(enabled);
}

void SendWidget::setSubtractFeeFromAmountEnabled(bool enabled) {
    ui->check_subtractFeeFromAmount->setVisible(enabled);
}

void SendWidget::onDataFromQR(const QString &data) {
    if (!data.isEmpty()) {
        QVariantMap uriData = m_wallet->parse_uri_to_object(data);
        if (!uriData.contains("error")) {
            ui->lineAddress->setText(uriData.value("address").toString());
            ui->lineDescription->setText(uriData.value("tx_description").toString());

            // Strip trailing zeroes
            auto amountStr = uriData.value("amount").toString();
            auto amount = WalletManager::amountFromString(amountStr);
            ui->lineAmount->setText(WalletManager::displayAmount(amount, false));
        } else {
            this->fillFromPaymentLink(data);
        }
    }
    else {
        Utils::showError(this, "Unable to decode QR code", "No QR code found.");
    }
}

// Biscuit: another coin's payment link, read the way Send expects it.
void SendWidget::fillFromPaymentLink(const QString &data) {
    const QString text = data.trimmed();
    const QString scheme = text.section(':', 0, 0).toLower();
    const qsizetype q = text.indexOf('?');
    if (scheme == "bitcoin" || scheme == "litecoin") {
        // Kept as a link, so that Send also reads its amount; the address in
        // lower case (QR codes are often in capitals).
        ui->lineAddress->setText(scheme + ":" + Utils::addressFromPaymentText(text) + (q < 0 ? QString() : text.mid(q)));
        return;
    }
    if (scheme != "ethereum") {
        ui->lineAddress->setText(text);
        return;
    }
    // Ethereum: a USDT or USDC payment request opens that tab, if it is added;
    // otherwise say so, rather than letting ETH go to someone expecting a token.
    const QString address = Utils::addressFromPaymentText(text);
    const QString path = text.mid(scheme.size() + 1).section('?', 0, 0);
    if (path.contains("/transfer")) {
        const QString contract = path.section('/', 0, 0).section('@', 0, 0).remove("pay-");
        const auto bytes = biscuit::coins::eth::parseAddress(contract);
        const auto *token = bytes ? biscuit::coins::eth::tokenByContract(*bytes) : nullptr;
        if (!token) {
            Utils::showError(this, "Unknown token", "This QR code asks for a token Biscuit doesn't hold. Only USDT and USDC on Ethereum can be sent.");
            return;
        }
        const int index = m_coinPicker->indexOf(token->symbol);
        if (index < 0 || !m_coinPicker->isCoinVisible(index)) {
            Utils::showError(this, QString("%1 isn't in this wallet").arg(token->symbol),
                             QString("This QR code asks for %1. Add %1 with + first, then scan it again.").arg(token->symbol));
            return;
        }
        m_coinPicker->setCurrentIndex(index);   // before the address: it then stays on this tab
    }
    ui->lineAddress->setText(address);
}

void SendWidget::setupComboBox() {
    ui->comboCurrencySelection->clear();

    QStringList defaultCurrencies = {"XMR", "USD", "EUR", "CNY", "JPY", "GBP"};
    QString preferredCurrency = conf()->get(Config::preferredFiatCurrency).toString();

    if (defaultCurrencies.contains(preferredCurrency)) {
        defaultCurrencies.removeOne(preferredCurrency);
    }

    ui->comboCurrencySelection->insertItems(0, defaultCurrencies);
    ui->comboCurrencySelection->insertItem(1, preferredCurrency);
}

void SendWidget::onPreferredFiatCurrencyChanged() {
    this->updateConversionLabel();
    this->setupComboBox();
}

void SendWidget::skinChanged() {
    if (ColorScheme::hasDarkBackground(this)) {
        ui->btnScan->setIcon(icons()->icon("camera_white.png"));
    } else {
        ui->btnScan->setIcon(icons()->icon("camera_dark.png"));
    }
}

SendWidget::~SendWidget() = default;