// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "swap/core/Amount.h"

using namespace biscuit::swap;

namespace {
    // Left in the wallet by "Max" for the network fee of the lock
    // transaction; the helper computes the exact fee and refuses if short.
    constexpr quint64 feeReserveSat = 3000;

    QLabel *wrappedLabel(const QString &text) {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        return label;
    }
}

AtomicSwapDialog::AtomicSwapDialog(const atomic::MakerOffer &offer, const QString &walletName, quint64 balanceSat,
                                   bool tor, double marketBtcPerXmr, QWidget *parent)
    : QDialog(parent)
    , m_offer(offer)
    , m_balanceSat(balanceSat)
    , m_amount(new QLineEdit(this))
    , m_receive(new QLabel(this))
    , m_problem(new QLabel(this))
    , m_start(nullptr)
{
    setWindowTitle("Atomic swap: BTC → XMR");
    setMinimumWidth(480);

    auto *form = new QFormLayout;
    form->addRow("Maker", new QLabel(offer.host()));

    QString price = QString("%1 BTC per XMR").arg(atomic::formatBtc(offer.priceSatPerXmr));
    if (const auto deviation = atomic::marketDeviation(offer.priceSatPerXmr, marketBtcPerXmr)) {
        price += QString(" (%1 vs market)").arg(atomic::formatDeviation(*deviation));
    }
    form->addRow("Price", new QLabel(price));
    form->addRow("From", new QLabel(QString("%1 · %2 BTC available").arg(walletName, atomic::formatBtc(balanceSat))));

    auto *amountRow = new QHBoxLayout;
    m_amount->setPlaceholderText(QString("%1 to %2").arg(atomic::formatBtc(offer.minSat), atomic::formatBtc(offer.maxSat)));
    amountRow->addWidget(m_amount);
    amountRow->addWidget(new QLabel("BTC"));
    auto *max = new QPushButton("Max", this);
    amountRow->addWidget(max);
    form->addRow("You send", amountRow);
    form->addRow("You get", m_receive);

    m_problem->setStyleSheet("color: #c0392b;");
    m_problem->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_problem);

    QStringList notes;
    notes << "The XMR arrives on a new subaddress of this wallet. The network fee of the Bitcoin "
             "transaction is paid on top of the amount.";
    notes << "Keep Biscuit open until the swap is done. If it is closed, the swap continues the next "
             "time you open this wallet.";
    notes << QString("If the maker does not send the XMR, your BTC comes back automatically after a "
                     "timeout of several hours%1.")
                 .arg(offer.refundDeposit > 0
                          ? QString(", minus the %1 the maker keeps as an anti-spam deposit").arg(atomic::formatDeposit(offer.refundDeposit))
                          : QString());
    notes << (tor ? QString("The maker is reached through Tor.")
                  : QString("Without Tor, the maker sees your IP address."));
    auto *info = wrappedLabel(notes.join("\n\n"));
    info->setStyleSheet("color: gray;");
    layout->addWidget(info);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_start = buttons->addButton("Start swap", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(max, &QPushButton::clicked, this, [this] {
        const quint64 spendable = m_balanceSat > feeReserveSat ? m_balanceSat - feeReserveSat : 0;
        m_amount->setText(atomic::formatBtc(std::min(spendable, m_offer.maxSat)));
    });
    connect(m_amount, &QLineEdit::textChanged, this, &AtomicSwapDialog::updateAmount);
    updateAmount();
}

void AtomicSwapDialog::updateAmount() {
    const QString text = m_amount->text().trimmed();
    const auto sat = amount::toAtomic(text, 8);
    m_btcSat = sat.value_or(0);

    QString problem;
    if (!text.isEmpty() && !sat) {
        problem = "Invalid amount (at most 8 decimals).";
    } else if (m_btcSat > 0 && m_btcSat < m_offer.minSat) {
        problem = QString("This maker swaps at least %1 BTC.").arg(atomic::formatBtc(m_offer.minSat));
    } else if (m_btcSat > m_offer.maxSat) {
        problem = QString("This maker swaps at most %1 BTC right now.").arg(atomic::formatBtc(m_offer.maxSat));
    } else if (m_btcSat >= m_balanceSat && m_btcSat > 0) {
        problem = "Not enough confirmed BTC for this amount and the network fee.";
    }
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());

    if (m_btcSat > 0 && m_offer.priceSatPerXmr > 0) {
        const quint64 xmr = static_cast<quint64>(static_cast<long double>(m_btcSat) * 1e12L / m_offer.priceSatPerXmr);
        m_receive->setText(QString("about %1 XMR").arg(amount::fromAtomic(xmr, 12)));
    } else {
        m_receive->setText("—");
    }
    m_start->setEnabled(m_btcSat > 0 && problem.isEmpty());
}
