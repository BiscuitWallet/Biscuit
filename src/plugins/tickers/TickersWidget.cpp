// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "TickersWidget.h"
#include "ui_TickersWidget.h"

#include "coins/CoinPicker.h"
#include "coins/CoinVault.h"
#include "utils/config.h"
#include "WindowManager.h"

TickersWidget::TickersWidget(QWidget *parent, Wallet *wallet)
    : QWidget(parent)
    , ui(new Ui::TickersWidget)
    , m_wallet(wallet)
{
    ui->setupUi(this);
    // Biscuit: Home's sections are separated by titles; this line was drawn
    // bright white in dark mode.
    ui->line->hide();
    this->setup();

    // TODO: this is a hack: find a better way to route settings signals to plugins
    connect(windowManager(), &WindowManager::updateBalance, this, &TickersWidget::updateBalance);
    connect(windowManager(), &WindowManager::preferredFiatCurrencyChanged, this, &TickersWidget::updateDisplay);
    connect(windowManager(), &WindowManager::pluginConfigured, [this](const QString &id) {
       if (id == "tickers") {
           this->setup();
       }
    });
    // Biscuit: a coin added to the wallet, or not, shows or hides its tickers.
    auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
    for (auto signal : {&biscuit::coins::CoinVault::unlocked, &biscuit::coins::CoinVault::locked,
                        &biscuit::coins::CoinVault::walletsChanged}) {
        connect(vault, signal, this, &TickersWidget::updateVisibility);
    }
    this->updateBalance();
}

void TickersWidget::setup() {
    QStringList tickers = conf()->get(Config::tickers).toStringList();

    Utils::clearLayout(ui->tickerLayout);
    Utils::clearLayout(ui->fiatTickerLayout);

    m_tickerWidgets.clear();
    m_tickerSymbols.clear();
    m_balanceTickerWidget.reset(nullptr);

    for (const auto &ticker : tickers) {
        if (ticker.contains("/")) { // ratio
            QStringList symbols = ticker.split("/");
            if (symbols.length() != 2) {
                qWarning() << "Invalid ticker in config: " << ticker;
            }
            auto* tickerWidget = new RatioTickerWidget(this, m_wallet, symbols[0], symbols[1]);
            m_tickerWidgets.append(tickerWidget);
            m_tickerSymbols.append(symbols);
            ui->tickerLayout->addWidget(tickerWidget);
        } else {
            auto* tickerWidget = new PriceTickerWidget(this, m_wallet, ticker);
            m_tickerWidgets.append(tickerWidget);
            m_tickerSymbols.append({ticker});
            ui->tickerLayout->addWidget(tickerWidget);
        }
    }

    if (conf()->get(Config::tickersShowFiatBalance).toBool()) {
        m_balanceTickerWidget.reset(new BalanceTickerWidget(this, m_wallet, false));
        ui->fiatTickerLayout->addWidget(m_balanceTickerWidget.data());
    }

    this->updateBalance();
    this->updateDisplay();
    this->updateVisibility();
}

// Biscuit: no BTC ticker (nor any ratio with BTC) when Bitcoin is not in this
// wallet; the same for Litecoin, Ethereum, USDT and USDC.
// While the wallet's coins are locked, which ones it holds is not known: all shown.
void TickersWidget::updateVisibility() {
    if (!m_wallet) {
        return;
    }
    const auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
    QStringList missing;
    for (const QString &ticker : biscuit::coins::addableTickers()) {
        if (vault->assetState(ticker) == biscuit::coins::CoinVault::CoinState::NotAdded) {
            missing << ticker;
        }
    }
    for (int i = 0; i < m_tickerWidgets.size() && i < m_tickerSymbols.size(); ++i) {
        bool shown = true;
        for (const QString &symbol : m_tickerSymbols.at(i)) {
            shown = shown && !missing.contains(symbol.trimmed(), Qt::CaseInsensitive);
        }
        m_tickerWidgets.at(i)->setVisible(shown);
    }
}

void TickersWidget::updateBalance() {
    ui->frame_fiatTickerLayout->setHidden(conf()->get(Config::hideBalance).toBool());
}

void TickersWidget::updateDisplay() {
    for (const auto &widget : m_tickerWidgets) {
        widget->updateDisplay();
    }
    if (m_balanceTickerWidget) {
        m_balanceTickerWidget->updateDisplay();
    }
}

TickersWidget::~TickersWidget() = default;