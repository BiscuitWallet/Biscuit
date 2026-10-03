// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinAddressesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Amount.h"
#include "CoinWallet.h"
#include "dialog/QrCodeDialog.h"
#include "qrcode/QrCode.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Column { ColIndex = 0, ColAddress, ColTransactions, ColBalance };
}

CoinAddressesDialog::CoinAddressesDialog(CoinWallet *coin, const QString &walletName, QWidget *parent)
    : WindowModalDialog(parent)
    , m_coin(coin)
{
    setWindowTitle(QString("%1 addresses · %2").arg(coin->params().name, walletName));
    resize(760, 460);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("All these addresses belong to this wallet and keep receiving payments. "
                             "Change addresses get back what is left over when you send.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *chainRow = new QHBoxLayout;
    m_chain = new QComboBox(this);
    m_chain->addItem("Receiving addresses", HdAccount::Receive);
    m_chain->addItem("Change addresses", HdAccount::Change);
    chainRow->addWidget(m_chain);
    chainRow->addStretch();
    layout->addLayout(chainRow);

    m_tree = new QTreeWidget(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setHeaderLabels({"#", "Address", "Transactions", "Balance"});
    m_tree->header()->setSectionResizeMode(ColAddress, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto showQr = [this](const QString &address) {
        // Uppercase bech32 makes a smaller QR code; wallets accept both cases.
        QrCode qr(address.toUpper(), QrCode::Version::AUTO, QrCode::ErrorCorrectionLevel::MEDIUM);
        QrCodeDialog dialog(this, &qr, address);
        dialog.exec();
    };
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this, showQr](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item) {
            return;
        }
        const QString address = item->text(ColAddress);
        QMenu menu(this);
        menu.addAction("Copy address", [address] { Utils::copyToClipboard(address); });
        menu.addAction("Show QR code", [showQr, address] { showQr(address); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [showQr](QTreeWidgetItem *item) {
        showQr(item->text(ColAddress));
    });

    connect(m_chain, &QComboBox::currentIndexChanged, this, &CoinAddressesDialog::refresh);
    connect(coin, &CoinWallet::updated, this, &CoinAddressesDialog::refresh);
    refresh();
}

void CoinAddressesDialog::refresh() {
    if (!m_coin) {
        return;
    }
    const auto chain = HdAccount::Chain(m_chain->currentData().toInt());
    const CoinParams &params = m_coin->params();
    auto format = [&params](quint64 sats) {
        return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(sats, params.decimals), params.ticker);
    };
    const QString shown = m_coin->receiveAddress();

    m_tree->clear();
    int used = 0;
    for (const auto &a : m_coin->addresses(chain)) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(ColIndex, QString::number(a.index));
        item->setTextAlignment(ColIndex, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(ColAddress, a.address);
        item->setText(ColTransactions, a.transactions > 0 ? QString::number(a.transactions) : QString("Unused"));
        item->setText(ColBalance, a.balance > 0 ? format(a.balance) : QString());
        item->setTextAlignment(ColBalance, Qt::AlignRight | Qt::AlignVCenter);
        if (a.address == shown) {
            item->setText(ColTransactions, item->text(ColTransactions) + ", shown on Receive");
            QFont bold = item->font(ColAddress);
            bold.setBold(true);
            for (int col = 0; col < m_tree->columnCount(); ++col) item->setFont(col, bold);
        }
        if (a.transactions > 0) ++used;
    }
    for (int col : {ColIndex, ColTransactions, ColBalance}) {
        m_tree->resizeColumnToContents(col);
    }

    if (chain == HdAccount::Change && m_tree->topLevelItemCount() == 0) {
        m_summary->setText("No change address used yet.");
    } else {
        m_summary->setText(QString("%1 used. Right-click an address to copy it or show its QR code.").arg(used));
    }
}

}
