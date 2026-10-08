// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "BalanceDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "Amount.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "libwalletqt/WalletManager.h"
#include "utils/Utils.h"
#include "utils/config.h"
#include "utils/AppData.h"
#include "constants.h"

namespace {
    enum Column { ColCoin = 0, ColSpendable, ColUnconfirmed, ColTotal, ColValue };
}

BalanceDialog::BalanceDialog(QWidget *parent, Wallet *wallet)
        : WindowModalDialog(parent)
        , m_wallet(wallet)
        , m_vault(biscuit::coins::CoinVault::forWallet(wallet))
{
    setWindowTitle("Balance");

    auto *layout = new QVBoxLayout(this);
    m_table = new QWidget(this);
    layout->addWidget(m_table);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet("color: gray;");
    layout->addWidget(m_note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(m_wallet, &Wallet::balanceUpdated, this, &BalanceDialog::updateBalance);
    connect(&appData()->prices, &Prices::cryptoPricesUpdated, this, &BalanceDialog::updateBalance);
    connect(&appData()->prices, &Prices::fiatPricesUpdated, this, &BalanceDialog::updateBalance);
    for (auto signal : {&biscuit::coins::CoinVault::unlocked, &biscuit::coins::CoinVault::locked,
                        &biscuit::coins::CoinVault::walletsChanged, &biscuit::coins::CoinVault::walletUpdated}) {
        connect(m_vault, signal, this, &BalanceDialog::updateBalance);
    }

    this->updateBalance();
    this->adjustSize();
}

void BalanceDialog::updateBalance() {
    // The grid is rebuilt each time: wallets can be added or removed.
    delete m_table->layout();
    qDeleteAll(m_table->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
    auto *grid = new QGridLayout(m_table);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);

    const QString fiat = conf()->get(Config::preferredFiatCurrency).toString();
    const bool showFiat = !conf()->get(Config::disableWebsocket).toBool();
    auto &prices = appData()->prices;
    double totalValue = 0;
    bool valueKnown = true;

    auto cell = [this, grid](int row, int column, const QString &text, bool bold = false) {
        auto *label = new QLabel(text, m_table);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (column != ColCoin) {
            label->setFont(Utils::getMonospaceFont());
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (bold) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        }
        grid->addWidget(label, row, column);
    };

    int row = 0;
    cell(row, ColSpendable, "Spendable", true);
    cell(row, ColUnconfirmed, "Unconfirmed", true);
    cell(row, ColTotal, "Total", true);
    if (showFiat) cell(row, ColValue, QString("Value (%1)").arg(fiat), true);

    // One row per wallet; the value column is filled when prices are known.
    auto addRow = [&](const QString &name, const QString &ticker, quint64 spendable, quint64 total,
                      const std::function<QString(quint64)> &format, double coins) {
        ++row;
        cell(row, ColCoin, name);
        cell(row, ColSpendable, format(spendable));
        cell(row, ColUnconfirmed, total > spendable ? format(total - spendable) : QString("–"));
        cell(row, ColTotal, format(total));
        if (showFiat && prices.canConvert(ticker, fiat)) {
            const double value = prices.convert(ticker, fiat, coins);
            totalValue += value;
            cell(row, ColValue, Utils::amountToCurrencyString(value, fiat));
        } else if (showFiat) {
            cell(row, ColValue, "price unknown");
            if (total > 0) valueKnown = false;
        }
    };

    addRow("Monero", "XMR", m_wallet->unlockedBalance(), m_wallet->balance(),
           [](quint64 a) { return WalletManager::displayAmount(a) + " XMR"; },
           m_wallet->balance() / constants::cdiv);

    QStringList notes;
    if (m_vault && m_vault->isUnlocked()) {
        for (const auto *params : {&biscuit::coins::bitcoin(), &biscuit::coins::litecoin()}) {
            const auto entries = m_vault->wallets(*params);
            for (const auto &entry : entries) {
                const auto balance = entry.wallet->balance();
                const QString name = entries.size() > 1 ? QString("%1 · %2").arg(params->name, entry.name) : params->name;
                addRow(name, params->ticker, balance.confirmed, balance.total(),
                       [params](quint64 a) {
                           return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(a, params->decimals), params->ticker);
                       },
                       double(balance.total()) / 1e8);
            }
        }
        // Ethereum: ETH, then each token there is some of (no unconfirmed
        // amount: a payment counts once it is in a block).
        const auto ethEntries = m_vault->wallets(biscuit::coins::ethereum());
        for (const auto &entry : ethEntries) {
            for (const QString &asset : biscuit::coins::EthWallet::assets()) {
                const auto amount = entry.eth->balance(asset);
                // ETH, and each token added to this wallet (at zero too); a
                // token not added still shows once some arrived.
                const bool added = m_vault->assetState(asset) == biscuit::coins::CoinVault::CoinState::Ready;
                if (!added && amount == 0) {
                    continue;
                }
                const int decimals = biscuit::coins::EthWallet::decimals(asset);
                const QString text = QString("%1 %2").arg(biscuit::coins::eth::formatAmount(amount, decimals), asset);
                QString name = asset == "ETH" ? QString("Ethereum") : QString("%1 (Ethereum)").arg(asset);
                if (ethEntries.size() > 1) name += QString(" · %1").arg(entry.name);
                ++row;
                cell(row, ColCoin, name);
                cell(row, ColSpendable, text);
                cell(row, ColUnconfirmed, "–");
                cell(row, ColTotal, text);
                if (showFiat && prices.canConvert(asset, fiat)) {
                    const double value = prices.convert(asset, fiat, biscuit::coins::eth::formatAmount(amount, decimals).toDouble());
                    totalValue += value;
                    cell(row, ColValue, Utils::amountToCurrencyString(value, fiat));
                } else if (showFiat) {
                    cell(row, ColValue, "price unknown");
                    if (amount > 0) valueKnown = false;
                }
            }
        }
    } else if (m_vault && m_vault->exists()) {
        notes << QString("Unlock %1 to see their balances.").arg(m_vault->mainSeedCoinsText());
    }

    if (showFiat) {
        ++row;
        auto *line = new QFrame(m_table);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        grid->addWidget(line, row, ColCoin, 1, ColValue + 1);
        ++row;
        cell(row, ColCoin, "Total value", true);
        cell(row, ColValue, valueKnown ? Utils::amountToCurrencyString(totalValue, fiat) : QString("…"), true);
        if (!valueKnown) notes << "Some prices are not known yet.";
    }

    notes << "Unconfirmed: Monero needs 10 confirmations (about 20 minutes) before it can be spent; "
             "Bitcoin and Litecoin are unconfirmed until included in a block.";
    m_note->setText(notes.join("\n"));
}

BalanceDialog::~BalanceDialog() = default;
