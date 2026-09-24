// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinTab.h"
#include "ui_CoinTab.h"

#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>

#include "Addresses.h"
#include "Amount.h"
#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "qrcode/QrCode.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Page { PageSetup = 0, PageUnlock, PageWallet };
    enum HistoryColumn { ColStatus = 0, ColAmount, ColFee, ColTx };

    struct FeeLevel {
        const char *label;
        int targetBlocks;
    };
    const FeeLevel feeLevels[] = {
        {"Fast (~20 min)", 2},
        {"Normal (~1 hour)", 6},
        {"Slow (~4 hours)", 24},
    };

    QString explorerUrl(const CoinParams &params, const QString &txid) {
        return params == litecoin() ? QString("https://litecoinspace.org/tx/%1").arg(txid)
                                    : QString("https://mempool.space/tx/%1").arg(txid);
    }
}

CoinTab::CoinTab(Wallet *wallet, const CoinParams &params, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CoinTab)
    , m_params(params)
    , m_vault(CoinVault::forWallet(wallet))
{
    ui->setupUi(this);

    QFont bold = ui->label_setupTitle->font();
    bold.setBold(true);
    ui->label_setupTitle->setFont(bold);
    ui->label_balance->setFont(bold);
    ui->label_unit->setText(params.ticker);
    ui->label_receiveHint->setStyleSheet("color: gray;");
    ui->label_unlockError->setStyleSheet("color: #c0392b;");
    for (const auto &level : feeLevels) {
        ui->combo_fee->addItem(level.label, level.targetBlocks);
    }
    ui->combo_fee->setCurrentIndex(1);
    ui->line_payTo->setPlaceholderText(QString("%1 address").arg(params.name));

    ui->tree_history->header()->setSectionResizeMode(ColTx, QHeaderView::Stretch);
    ui->tree_history->header()->setStretchLastSection(false);
    ui->tree_history->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tree_history, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = ui->tree_history->itemAt(pos);
        if (!item) return;
        const QString txid = item->text(ColTx);
        QMenu menu(this);
        menu.addAction("Copy transaction ID", [txid] { Utils::copyToClipboard(txid); });
        menu.addAction("View on block explorer", [this, txid] { Utils::externalLinkWarning(this, explorerUrl(m_params, txid)); });
        menu.exec(ui->tree_history->viewport()->mapToGlobal(pos));
    });

    connect(ui->btn_create, &QPushButton::clicked, this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Create, this).exec();
    });
    connect(ui->btn_restore, &QPushButton::clicked, this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Restore, this).exec();
    });
    connect(ui->btn_unlock, &QPushButton::clicked, this, &CoinTab::unlock);
    connect(ui->line_password, &QLineEdit::returnPressed, this, &CoinTab::unlock);
    connect(ui->btn_showSeed, &QPushButton::clicked, this, [this] { showCoinSeed(m_vault, this); });
    connect(ui->btn_lock, &QPushButton::clicked, this, [this] { m_vault->lock(); });
    connect(ui->btn_copyAddress, &QPushButton::clicked, this, [this] { Utils::copyToClipboard(ui->line_address->text()); });
    connect(ui->btn_max, &QPushButton::clicked, this, [this] {
        m_sendAll = true;
        ui->line_amount->setText("All");
        ui->line_amount->setReadOnly(true);
    });
    connect(ui->btn_clear, &QPushButton::clicked, this, &CoinTab::clearSend);
    connect(ui->btn_send, &QPushButton::clicked, this, &CoinTab::send);
    connect(ui->combo_fee, &QComboBox::currentIndexChanged, this, &CoinTab::refreshFees);

    connect(m_vault, &CoinVault::unlocked, this, &CoinTab::updatePage);
    connect(m_vault, &CoinVault::locked, this, &CoinTab::updatePage);
    updatePage();
}

CoinTab::~CoinTab() = default;

void CoinTab::updatePage() {
    if (m_vault->isUnlocked()) {
        ui->stack->setCurrentIndex(PageWallet);
        bindWallet();
    } else {
        m_coin = nullptr;
        ui->stack->setCurrentIndex(m_vault->exists() ? PageUnlock : PageSetup);
    }
}

void CoinTab::bindWallet() {
    CoinWallet *coin = m_vault->wallet(m_params);
    if (!coin || coin == m_coin) {
        return;
    }
    m_coin = coin;
    connect(coin, &CoinWallet::updated, this, &CoinTab::refresh);
    connect(coin, &CoinWallet::statusChanged, this, &CoinTab::refresh);
    refresh();
}

void CoinTab::unlock() {
    ui->label_unlockError->setText("Unlocking…");
    QCoreApplication::processEvents();
    QString error;
    const bool ok = m_vault->unlock(ui->line_password->text(), &error);
    ui->line_password->clear();
    ui->label_unlockError->setText(ok ? QString() : error);
}

QString CoinTab::formatAmount(qint64 sats, bool sign) const {
    const QString value = biscuit::swap::amount::fromAtomic(quint64(std::llabs(sats)), m_params.decimals);
    const QString prefix = sats < 0 ? "-" : (sign && sats > 0 ? "+" : "");
    return QString("%1%2 %3").arg(prefix, value, m_params.ticker);
}

void CoinTab::refresh() {
    if (!m_coin) {
        return;
    }
    const auto balance = m_coin->balance();
    QString balanceText = QString("%1  %2").arg(m_params.name, formatAmount(qint64(balance.confirmed)));
    if (balance.unconfirmed > 0) {
        balanceText += QString("  (+%1 unconfirmed)").arg(formatAmount(qint64(balance.unconfirmed)));
    }
    ui->label_balance->setText(balanceText);

    switch (m_coin->status()) {
        case CoinWallet::Status::Disconnected: ui->label_status->setText("Disconnected"); break;
        case CoinWallet::Status::Connecting: ui->label_status->setText("Connecting…"); break;
        case CoinWallet::Status::Synchronizing: ui->label_status->setText("Synchronizing… · " + m_coin->serverName()); break;
        case CoinWallet::Status::Synchronized: ui->label_status->setText("Synchronized · " + m_coin->serverName()); break;
    }

    // History
    ui->tree_history->clear();
    const int tip = m_coin->blockHeight();
    for (const auto &e : m_coin->history()) {
        auto *item = new QTreeWidgetItem(ui->tree_history);
        if (e.height <= 0) {
            item->setText(ColStatus, "Unconfirmed");
        } else {
            const int confirmations = tip > 0 ? tip - e.height + 1 : 1;
            item->setText(ColStatus, confirmations >= 6 ? QString("Confirmed") : QString("%1 confirmations").arg(confirmations));
        }
        item->setText(ColAmount, formatAmount(e.delta, true));
        item->setTextAlignment(ColAmount, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(ColFee, e.fee && e.delta < 0 ? formatAmount(qint64(*e.fee)) : QString());
        item->setText(ColTx, e.txid);
    }
    for (int c = ColStatus; c < ColTx; ++c) {
        ui->tree_history->resizeColumnToContents(c);
    }

    // Receive
    const QString address = m_coin->receiveAddress();
    if (ui->line_address->text() != address) {
        ui->line_address->setText(address);
        ui->line_address->setCursorPosition(0);
        const QrCode qr(address.toUpper(), QrCode::Version::AUTO, QrCode::ErrorCorrectionLevel::MEDIUM);
        if (qr.isValid()) {
            ui->label_qr->setPixmap(qr.toPixmap(1).scaled(ui->label_qr->size(), Qt::KeepAspectRatio));
        }
    }
    refreshFees();
}

void CoinTab::refreshFees() {
    if (m_coin) {
        ui->label_feeRate->setText(QString("%1 sat/vB").arg(m_coin->feeRate(ui->combo_fee->currentData().toInt()), 0, 'f', 1));
    }
}

void CoinTab::clearSend() {
    ui->line_payTo->clear();
    ui->line_amount->clear();
    ui->line_amount->setReadOnly(false);
    m_sendAll = false;
}

void CoinTab::send() {
    if (!m_coin) {
        return;
    }
    const QString address = ui->line_payTo->text().trimmed();
    if (!isValidAddress(address, m_params)) {
        Utils::showError(this, "Invalid address", QString("This is not a valid %1 address.").arg(m_params.name));
        return;
    }
    quint64 amount = 0;
    if (!m_sendAll) {
        const auto parsed = biscuit::swap::amount::toAtomic(ui->line_amount->text().trimmed(), m_params.decimals);
        if (!parsed || *parsed == 0) {
            Utils::showError(this, "Invalid amount", QString("Enter an amount in %1, with at most 8 decimals.").arg(m_params.ticker));
            return;
        }
        amount = *parsed;
    }
    if (m_coin->status() != CoinWallet::Status::Synchronized) {
        Utils::showError(this, "Not synchronized", "Wait until the wallet is synchronized, then try again.");
        return;
    }

    const double feeRate = m_coin->feeRate(ui->combo_fee->currentData().toInt());
    QString error;
    const auto plan = m_coin->planSend(address, amount, feeRate, m_sendAll, &error);
    if (!plan) {
        Utils::showError(this, "Unable to send", error);
        return;
    }

    // Explicit confirmation: nothing is signed or sent before this.
    QMessageBox box(this);
    box.setWindowTitle(QString("Send %1").arg(m_params.name));
    box.setIcon(QMessageBox::Question);
    box.setText(QString("Send %1?").arg(formatAmount(qint64(plan->amount))));
    box.setInformativeText(QString("To: %1\nNetwork fee: %2 (%3 sat/vB)\nTotal: %4")
                           .arg(address, formatAmount(qint64(plan->fee)))
                           .arg(feeRate, 0, 'f', 1)
                           .arg(formatAmount(qint64(plan->amount + plan->fee))));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    ui->btn_send->setEnabled(false);
    m_coin->broadcast(*plan, [this](const QString &txid, const QString &error) {
        ui->btn_send->setEnabled(true);
        if (!error.isEmpty()) {
            Utils::showError(this, "Transaction not sent", error);
            return;
        }
        clearSend();
        Utils::showInfo(this, "Transaction sent", QString("Transaction ID: %1").arg(txid));
        ui->tabs->setCurrentWidget(ui->tab_history);
    });
}

}
