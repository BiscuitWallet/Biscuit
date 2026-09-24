// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapTradeDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "DemoSwap.h"
#include "qrcode/QrCode.h"
#include "utils/Utils.h"

using namespace biscuit::swap;

SwapTradeDialog::SwapTradeDialog(SwapManager *manager, const QString &providerId, const QString &tradeId, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_providerId(providerId)
    , m_tradeId(tradeId)
{
    this->setWindowTitle("Swap details");
    auto *layout = new QVBoxLayout(this);

    m_status = new QLabel(this);
    QFont bold = m_status->font();
    bold.setBold(true);
    m_status->setFont(bold);
    layout->addWidget(m_status);

    auto *body = new QHBoxLayout;
    m_details = new QLabel(this);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setWordWrap(true);
    m_qr = new QLabel(this);
    m_qr->setFixedSize(180, 180);
    body->addWidget(m_details, 1);
    body->addWidget(m_qr, 0, Qt::AlignTop);
    layout->addLayout(body);

    m_support = new QLabel(this);
    m_support->setWordWrap(true);
    m_support->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_support->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_support);

    auto *buttons = new QDialogButtonBox(this);
    m_btnCopyAddress = buttons->addButton("Copy deposit address", QDialogButtonBox::ActionRole);
    m_btnSend = buttons->addButton("Send from this wallet…", QDialogButtonBox::ActionRole);
    auto *btnRefresh = buttons->addButton("Refresh", QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btnRefresh, &QPushButton::clicked, m_manager, &SwapManager::refreshNow);
    connect(m_btnCopyAddress, &QPushButton::clicked, this, [this] {
        if (const Trade *t = m_manager->trade(m_providerId, m_tradeId)) {
            Utils::copyToClipboard(t->depositAddress);
        }
    });
    connect(m_btnSend, &QPushButton::clicked, this, [this] {
        if (const Trade *t = m_manager->trade(m_providerId, m_tradeId)) {
            emit sendDepositRequested(*t);
        }
    });
    connect(m_manager, &SwapManager::tradesChanged, this, &SwapTradeDialog::updateView);

    updateView();
    this->resize(640, this->sizeHint().height());
}

void SwapTradeDialog::updateView() {
    const Trade *t = m_manager->trade(m_providerId, m_tradeId);
    if (!t) {
        m_status->setText("Swap not found");
        return;
    }

    const bool demo = t->providerId == demo::providerId;
    m_status->setText(QString("%1%2").arg(demo ? "[Demo] " : "", tradeStatusDescription(t->status)));

    const QString fromTicker = t->from.ticker.toUpper();
    QString details;
    details += QString("<b>Send exactly:</b> %1 %2<br>").arg(t->amountFrom, t->from.displayName());
    details += QString("<b>To this deposit address:</b><br><tt>%1</tt><br>").arg(t->depositAddress.toHtmlEscaped());
    if (!t->depositMemo.isEmpty()) {
        details += QString("<b>Memo / destination tag (required):</b> <tt>%1</tt><br>").arg(t->depositMemo.toHtmlEscaped());
    }
    details += "<br>";
    details += QString("<b>You receive (estimated):</b> %1 %2<br>").arg(t->amountTo, t->to.displayName());
    details += QString("<b>At:</b> <tt>%1</tt><br>").arg(t->payoutAddress.toHtmlEscaped());
    if (!t->refundAddress.isEmpty()) {
        details += QString("<b>Refund address:</b> <tt>%1</tt><br>").arg(t->refundAddress.toHtmlEscaped());
    }
    details += QString("<b>Rate:</b> %1<br>").arg(t->rateType == RateType::Fixed ? "fixed" : "floating");
    details += QString("<b>Exchange:</b> %1 (KYC rating %2)<br>").arg(t->exchange.toHtmlEscaped(), kycRatingToString(t->kycRating));
    if (!t->depositTxId.isEmpty()) {
        details += QString("<b>Your deposit transaction:</b> <tt>%1</tt><br>").arg(t->depositTxId);
    }
    m_details->setText(details);

    const QrCode qr(t->depositAddress, QrCode::Version::AUTO, QrCode::ErrorCorrectionLevel::MEDIUM);
    if (qr.isValid() && !demo) {
        m_qr->setPixmap(qr.toPixmap(1).scaled(m_qr->size(), Qt::KeepAspectRatio));
    } else {
        m_qr->setText(demo ? "No QR code\nfor a demo swap" : QString());
        m_qr->setAlignment(Qt::AlignCenter);
    }

    // Identifiers are always shown: they are what support asks for, and the
    // aggregator forgets the trade after 14 days.
    QString support = QString("<b>Swap identifiers</b> — keep them in case you need support<br>"
                              "Trade ID: <tt>%1</tt><br>").arg(t->tradeId.toHtmlEscaped());
    if (!t->exchangeTradeId.isEmpty()) {
        support += QString("Exchange trade ID: <tt>%1</tt><br>").arg(t->exchangeTradeId.toHtmlEscaped());
    }
    if (!t->exchangePassword.isEmpty()) {
        support += QString("Exchange password: <tt>%1</tt><br>").arg(t->exchangePassword.toHtmlEscaped());
    }
    if (needsSupport(t->status)) {
        support += QString("<br><b>This swap needs attention.</b> Contact support with the identifiers above");
        support += t->supportUrl.isEmpty() ? QString(".") : QString(": <tt>%1</tt>").arg(t->supportUrl.toHtmlEscaped());
    }
    m_support->setText(support);

    const bool fromThisWallet = fromTicker == QLatin1String("XMR");
    const bool awaitingDeposit = t->status == TradeStatus::New || t->status == TradeStatus::Waiting;
    m_btnSend->setVisible(fromThisWallet && !demo);
    m_btnSend->setEnabled(awaitingDeposit && t->depositTxId.isEmpty());
    m_btnCopyAddress->setEnabled(!demo);
}
