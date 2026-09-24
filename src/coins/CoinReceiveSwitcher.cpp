// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinReceiveSwitcher.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "qrcode/QrCode.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

namespace biscuit::coins {

CoinReceiveSwitcher::CoinReceiveSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent)
    : QWidget(parent)
    , m_vault(CoinVault::forWallet(wallet))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Receive", this));
    m_coin = new QComboBox(this);
    m_coin->addItem(QIcon(":/assets/images/appicons/monero.png"), "Monero");
    m_coin->addItem(icons()->icon("bitcoin.png"), "Bitcoin");
    m_coin->addItem(icons()->icon("litecoin.png"), "Litecoin");
    top->addWidget(m_coin);
    top->addStretch();
    layout->addLayout(top);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(moneroPage);
    m_pages->addWidget(coinPage(bitcoin()));
    m_pages->addWidget(coinPage(litecoin()));
    layout->addWidget(m_pages);

    connect(m_coin, &QComboBox::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_vault, &CoinVault::unlocked, this, [this] {
        for (CoinWallet *w : {m_vault->bitcoin(), m_vault->litecoin()}) {
            connect(w, &CoinWallet::updated, this, &CoinReceiveSwitcher::refresh);
        }
        refresh();
    });
    connect(m_vault, &CoinVault::locked, this, &CoinReceiveSwitcher::refresh);
    refresh();
}

QWidget *CoinReceiveSwitcher::coinPage(const CoinParams &params) {
    CoinWidgets w;
    w.params = &params;
    w.state = new QStackedWidget(this);

    // Not set up (or locked): one button, the same flow as from Send.
    auto *setup = new QWidget(w.state);
    auto *setupLayout = new QVBoxLayout(setup);
    auto *setupText = new QLabel(QString("%1 is not set up in this wallet yet. Bitcoin and Litecoin share one seed "
                                         "phrase (BIP39), separate from your Monero seed.").arg(params.name), setup);
    setupText->setWordWrap(true);
    setupLayout->addWidget(setupText);
    auto *buttons = new QHBoxLayout;
    auto *btnCreate = new QPushButton("Create new seed", setup);
    auto *btnRestore = new QPushButton("Restore from seed", setup);
    buttons->addWidget(btnCreate);
    buttons->addWidget(btnRestore);
    buttons->addStretch();
    setupLayout->addLayout(buttons);
    setupLayout->addStretch();
    connect(btnCreate, &QPushButton::clicked, this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Create, this).exec();
    });
    connect(btnRestore, &QPushButton::clicked, this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Restore, this).exec();
    });
    w.state->addWidget(setup);

    // Address page, same layout as the other receive screens.
    auto *page = new QWidget(w.state);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setSpacing(12);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto *addressRow = new QHBoxLayout;
    w.address = new QLineEdit(page);
    w.address->setReadOnly(true);
    auto *copy = new QPushButton("Copy", page);
    addressRow->addWidget(w.address);
    addressRow->addWidget(copy);
    form->addRow("Address", addressRow);
    auto *hint = new QLabel("A new address is shown after each payment received, for privacy. "
                            "Previous addresses keep working.", page);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");
    hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    form->addRow(QString(), hint);
    pageLayout->addLayout(form, 1);
    w.qr = new QLabel(page);
    w.qr->setFixedSize(180, 180);
    w.qr->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(w.qr, 0, Qt::AlignTop);
    connect(copy, &QPushButton::clicked, this, [address = w.address] { Utils::copyToClipboard(address->text()); });
    w.state->addWidget(page);

    m_coinWidgets.append(w);
    return w.state;
}

void CoinReceiveSwitcher::refresh() {
    for (CoinWidgets &w : m_coinWidgets) {
        CoinWallet *coin = m_vault->isUnlocked() ? m_vault->wallet(*w.params) : nullptr;
        w.state->setCurrentIndex(coin ? 1 : 0);
        if (!coin) {
            continue;
        }
        const QString address = coin->receiveAddress();
        if (w.address->text() == address) {
            continue;
        }
        w.address->setText(address);
        w.address->setCursorPosition(0);
        // Uppercase bech32 makes a smaller QR code; wallets accept both cases.
        const QrCode qr(address.toUpper(), QrCode::Version::AUTO, QrCode::ErrorCorrectionLevel::MEDIUM);
        if (qr.isValid()) {
            w.qr->setPixmap(qr.toPixmap(1).scaled(w.qr->size(), Qt::KeepAspectRatio));
        }
    }
}

}
