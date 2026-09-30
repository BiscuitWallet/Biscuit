// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "SignVerifyDialog.h"
#include "ui_SignVerifyDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QMessageBox>

#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "coins/core/Addresses.h"
#include "coins/core/MessageSigning.h"
#include "utils/Utils.h"

SignVerifyDialog::SignVerifyDialog(Wallet *wallet, QWidget *parent)
        : WindowModalDialog(parent)
        , ui(new Ui::SignVerifyDialog)
        , m_wallet(wallet)
{
    ui->setupUi(this);

    connect(ui->btn_Sign, &QPushButton::clicked, this, &SignVerifyDialog::signMessage);
    connect(ui->btn_Verify, &QPushButton::clicked, this, &SignVerifyDialog::verifyMessage);
    connect(ui->btn_Copy, &QPushButton::clicked, this, &SignVerifyDialog::copyToClipboard);

    connect(ui->message, &QPlainTextEdit::textChanged, [this](){ui->btn_Copy->setVisible(false);});
    connect(ui->address, &QLineEdit::textEdited, [this](){ui->btn_Copy->setVisible(false);});
    connect(ui->signature, &QLineEdit::textEdited, [this](){ui->btn_Copy->setVisible(false);});

    ui->address->setText(m_wallet->address(0, 0));
    ui->address->setCursorPosition(0);

    // Biscuit: which coin to sign with; it fills in one of its addresses.
    // Verification follows the address, whatever the coin chosen.
    m_coin = new QComboBox(this);
    m_coin->addItems({"Monero", "Bitcoin", "Litecoin"});
    ui->gridLayout->addWidget(new QLabel("Coin:", this), 2, 0);
    ui->gridLayout->addWidget(m_coin, 2, 1, Qt::AlignLeft);
    connect(m_coin, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
        const biscuit::coins::CoinParams *params = index == 1 ? &biscuit::coins::bitcoin()
                                                 : index == 2 ? &biscuit::coins::litecoin() : nullptr;
        if (!params) {
            ui->address->setText(m_wallet->address(0, 0));
        } else if (vault->isUnlocked() && vault->wallet(*params)) {
            ui->address->setText(vault->wallet(*params)->receiveAddress());
        } else {
            ui->address->clear();
            ui->address->setPlaceholderText(QString("Unlock %1 to sign with one of its addresses").arg(params->name));
        }
        ui->address->setCursorPosition(0);
        ui->btn_Copy->setVisible(false);
    });

    if (m_wallet->isHwBacked()) {
        // We don't have the secret spend key to sign messages
        ui->btn_Sign->setEnabled(false);
        ui->btn_Sign->setToolTip("Message signing is not supported on this hardware device.");
    }

    ui->btn_Copy->setVisible(false);
}

const biscuit::coins::CoinParams *SignVerifyDialog::coinOf(const QString &address) const {
    for (const auto *params : {&biscuit::coins::bitcoin(), &biscuit::coins::litecoin()}) {
        if (biscuit::coins::isValidAddress(address.trimmed(), *params)) {
            return params;
        }
    }
    return nullptr;
}

void SignVerifyDialog::signMessage() {
    if (const auto *params = coinOf(ui->address->text())) {
        auto *vault = biscuit::coins::CoinVault::forWallet(m_wallet);
        if (!vault->isUnlocked()) {
            Utils::showError(this, "Unable to sign", QString("Unlock %1 first (Receive or Send tab).").arg(params->name));
            return;
        }
        // The address may belong to any of this coin's wallets.
        QString error;
        for (const auto &entry : vault->wallets(*params)) {
            const QString signature = entry.wallet->signMessage(ui->address->text().trimmed(), ui->message->toPlainText(), &error);
            if (!signature.isEmpty()) {
                ui->signature->setText(signature);
                ui->btn_Copy->setVisible(true);
                return;
            }
        }
        Utils::showError(this, "Unable to sign", error);
        return;
    }

    QString signature = m_wallet->signMessage(ui->message->toPlainText(), false, ui->address->text());

    if (signature.isEmpty()) {
        QMessageBox::information(this, "Information", m_wallet->errorString());
        return;
    }

    ui->signature->setText(signature);
    ui->btn_Copy->setVisible(true);
}

void SignVerifyDialog::verifyMessage() {
    const auto *params = coinOf(ui->address->text());
    const bool verified = params
        ? biscuit::coins::message::verify(ui->address->text(), ui->message->toPlainText(), ui->signature->text(), *params)
        : m_wallet->verifySignedMessage(ui->message->toPlainText(), ui->address->text(), ui->signature->text());

    if (verified) {
        Utils::showInfo(this, "Signature is valid");
    } else {
        Utils::showError(this, "Signature is not valid");
    }
}

void SignVerifyDialog::copyToClipboard() {
    QStringList sig;
    sig << ui->message->toPlainText() << ui->address->text() << ui->signature->text();
    Utils::copyToClipboard(sig.join("\n"));
}

SignVerifyDialog::~SignVerifyDialog() = default;