// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinReceiveSwitcher.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "CoinAddressesDialog.h"
#include "CoinPicker.h"
#include "CoinWalletBar.h"
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

    m_coin = new CoinPicker(this);
    addWalletCoins(m_coin);
    layout->addWidget(m_coin);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(moneroPage);
    m_pages->addWidget(coinPage(bitcoin()));
    m_pages->addWidget(coinPage(litecoin()));
    layout->addWidget(m_pages);

    connect(m_coin, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged, &CoinVault::walletUpdated}) {
        connect(m_vault, signal, this, &CoinReceiveSwitcher::refresh);
    }
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
    auto *actions = new QHBoxLayout;
    auto *fresh = new QPushButton("New address", page);
    fresh->setToolTip("Show another address of this wallet, never used yet. "
                      "Earlier addresses keep working.");
    auto *all = new QPushButton("All addresses…", page);
    actions->addWidget(fresh);
    actions->addWidget(all);
    actions->addStretch();
    form->addRow(QString(), actions);
    w.keep = new QCheckBox("Keep this address after payments", page);
    form->addRow(QString(), w.keep);
    w.hint = new QLabel(page);
    w.hint->setWordWrap(true);
    w.hint->setStyleSheet("color: gray;");
    w.hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    form->addRow(QString(), w.hint);
    pageLayout->addLayout(form, 1);
    w.qr = new QLabel(page);
    w.qr->setFixedSize(180, 180);
    w.qr->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(w.qr, 0, Qt::AlignTop);
    connect(copy, &QPushButton::clicked, this, [address = w.address] { Utils::copyToClipboard(address->text()); });
    const CoinParams *p = &params;
    connect(fresh, &QPushButton::clicked, this, [this, p] {
        CoinWallet *c = coin(*p);
        if (c && !c->newReceiveAddress()) {
            Utils::showError(this, "No new address",
                             "The last 20 addresses shown have not received anything yet. Use one of them, "
                             "or wait for a payment: if this wallet were restored from its seed, payments "
                             "to addresses further on would not be found.");
        }
    });
    connect(all, &QPushButton::clicked, this, [this, p] {
        if (CoinWallet *c = coin(*p)) {
            CoinAddressesDialog(c, m_vault->selectedName(*p), this).exec();
        }
    });
    connect(w.keep, &QCheckBox::toggled, this, [this, p](bool keep) {
        if (CoinWallet *c = coin(*p)) c->setKeepReceiveAddress(keep);
    });

    // Wallet buttons ([Main] [Litecoin 2] [+ Add]) above the address.
    auto *withBar = new QWidget(w.state);
    auto *withBarLayout = new QVBoxLayout(withBar);
    withBarLayout->setContentsMargins(0, 0, 0, 0);
    withBarLayout->setSpacing(16);
    withBarLayout->addWidget(new CoinWalletBar(m_vault, params, withBar));
    withBarLayout->addWidget(page);
    withBarLayout->addStretch(1);   // everything at the top
    w.state->addWidget(withBar);

    m_coinWidgets.append(w);
    return w.state;
}

CoinWallet *CoinReceiveSwitcher::coin(const CoinParams &params) const {
    return m_vault->isUnlocked() ? m_vault->wallet(params) : nullptr;
}

void CoinReceiveSwitcher::refresh() {
    for (CoinWidgets &w : m_coinWidgets) {
        CoinWallet *coin = this->coin(*w.params);
        w.state->setCurrentIndex(coin ? 1 : 0);
        if (!coin) {
            continue;
        }
        const bool keep = coin->keepsReceiveAddress();
        {
            const QSignalBlocker blocker(w.keep);
            w.keep->setChecked(keep);
        }
        w.hint->setText(keep
            ? "This address stays the same. Anyone who knows it can look it up on the blockchain "
              "and see every payment it receives."
            : "Once this address receives a payment, a new one is shown, so that people who pay you "
              "cannot see your other payments. Earlier addresses keep working: see All addresses.");
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
