// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapTradeDialog.h"
#include "ui_SwapTradeDialog.h"

#include "DemoSwap.h"
#include "qrcode/QrCode.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

using namespace biscuit::swap;

namespace {
    void setRowVisible(QLabel *title, QWidget *field, bool visible) {
        title->setVisible(visible);
        field->setVisible(visible);
    }
}

SwapTradeDialog::SwapTradeDialog(SwapManager *manager, const QString &providerId, const QString &tradeId, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SwapTradeDialog)
    , m_manager(manager)
    , m_providerId(providerId)
    , m_tradeId(tradeId)
{
    ui->setupUi(this);

    QFont bold = ui->label_status->font();
    bold.setBold(true);
    ui->label_status->setFont(bold);

    connect(ui->btn_close, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btn_refresh, &QPushButton::clicked, m_manager, &SwapManager::refreshNow);
    connect(ui->btn_copyDeposit, &QPushButton::clicked, this, [this] {
        Utils::copyToClipboard(ui->line_deposit->text());
    });
    connect(ui->btn_send, &QPushButton::clicked, this, [this] {
        if (const Trade *t = m_manager->trade(m_providerId, m_tradeId)) {
            emit sendDepositRequested(*t);
        }
    });
    connect(m_manager, &SwapManager::tradesChanged, this, &SwapTradeDialog::updateView);

    updateView();
    this->adjustSize();
}

SwapTradeDialog::~SwapTradeDialog() = default;

void SwapTradeDialog::updateView() {
    const Trade *t = m_manager->trade(m_providerId, m_tradeId);
    if (!t) {
        ui->label_status->setText("Swap not found");
        return;
    }

    const bool demo = t->providerId == demo::providerId;
    ui->label_status->setText(QString("%1%2 → %3: %4")
                              .arg(demo ? "[Demo] " : "", t->from.displayName(), t->to.displayName(),
                                   tradeStatusText(*t)));

    if (needsSupport(t->status)) {
        ui->frame_attention->setInfo(icons()->icon("warning.png"),
                                     "This swap needs attention. Contact support with the identifiers below.");
        ui->frame_attention->show();
    } else if (demo) {
        ui->frame_attention->setInfo(icons()->icon("info2.svg"), "Simulated swap: nothing needs to be sent.");
        ui->frame_attention->show();
    } else {
        ui->frame_attention->hide();
    }

    ui->label_send->setText(QString("%1 %2").arg(t->amountFrom, t->from.ticker.toUpper()));
    ui->line_deposit->setText(t->depositAddress);
    ui->line_deposit->setCursorPosition(0);
    ui->btn_copyDeposit->setEnabled(!demo);
    setRowVisible(ui->label_memoTitle, ui->line_memo, !t->depositMemo.isEmpty());
    ui->line_memo->setText(t->depositMemo);
    if (!t->depositMemo.isEmpty()) {
        ui->label_memoTitle->setText("Memo (required)");
    }

    ui->label_get->setText(QString("≈ %1 %2").arg(t->amountTo, t->to.ticker.toUpper()));
    ui->line_payout->setText(t->payoutAddress);
    ui->line_payout->setCursorPosition(0);
    setRowVisible(ui->label_refundTitle, ui->line_refund, !t->refundAddress.isEmpty());
    ui->line_refund->setText(t->refundAddress);
    ui->line_refund->setCursorPosition(0);
    ui->label_exchange->setText(QString("%1 · %2 rate")
                                .arg(t->exchange, t->rateType == RateType::Fixed ? "fixed" : "floating"));
    setRowVisible(ui->label_txTitle, ui->line_tx, !t->depositTxId.isEmpty());
    ui->line_tx->setText(t->depositTxId);

    const QrCode qr(t->depositAddress, QrCode::Version::AUTO, QrCode::ErrorCorrectionLevel::MEDIUM);
    ui->label_qr->setVisible(!demo && qr.isValid());
    if (!demo && qr.isValid()) {
        ui->label_qr->setPixmap(qr.toPixmap(1).scaled(ui->label_qr->size(), Qt::KeepAspectRatio));
    }

    // Identifiers are always shown: support asks for them, and the aggregator
    // forgets the trade after 14 days.
    ui->line_tradeId->setText(t->tradeId);
    setRowVisible(ui->label_exchangeIdTitle, ui->line_exchangeId, !t->exchangeTradeId.isEmpty());
    ui->line_exchangeId->setText(t->exchangeTradeId);
    setRowVisible(ui->label_passwordTitle, ui->line_password, !t->exchangePassword.isEmpty());
    ui->line_password->setText(t->exchangePassword);
    setRowVisible(ui->label_supportTitle, ui->line_support, !t->supportUrl.isEmpty());
    ui->line_support->setText(t->supportUrl);

    // Biscuit holds XMR, BTC, LTC, and ETH, USDT and USDC on Ethereum: the
    // deposit can be sent from here (or the user is told how, if not added).
    const QString f = t->from.ticker;
    const bool fromThisWallet = f == QLatin1String("xmr")
                                || ((f == QLatin1String("btc") || f == QLatin1String("ltc")) && t->from.network == QLatin1String("Mainnet"))
                                || ((f == QLatin1String("eth") || f == QLatin1String("usdt") || f == QLatin1String("usdc"))
                                    && t->from.network == QLatin1String("ERC20"));
    const bool awaitingDeposit = t->status == TradeStatus::New || t->status == TradeStatus::Waiting;
    ui->btn_send->setVisible(fromThisWallet && !demo);
    ui->btn_send->setEnabled(awaitingDeposit && t->depositTxId.isEmpty());
}
