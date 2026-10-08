// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinContactsSwitcher.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Addresses.h"
#include "CoinPicker.h"
#include "CoinVault.h"
#include "libwalletqt/Wallet.h"
#include "utils/Utils.h"

namespace biscuit::coins {

namespace {
    enum Column { ColName = 0, ColAddress };
    constexpr int IndexRole = Qt::UserRole;
}

CoinContactsSwitcher::CoinContactsSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent)
    : QWidget(parent)
    , m_wallet(wallet)
    , m_vault(CoinVault::forWallet(wallet))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_picker = new CoinPicker(this);
    addWalletCoins(m_picker, true, false);
    layout->addWidget(m_picker);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(moneroPage);
    m_btc = new QTreeWidget(this);
    m_ltc = new QTreeWidget(this);
    m_btcStatus = new QLabel(this);
    m_ltcStatus = new QLabel(this);
    m_eth = new QTreeWidget(this);
    m_ethStatus = new QLabel(this);
    m_pages->addWidget(makePage(bitcoin(), m_btc, m_btcStatus));
    m_pages->addWidget(makePage(litecoin(), m_ltc, m_ltcStatus));
    m_pages->addWidget(makePage(ethereum(), m_eth, m_ethStatus));
    layout->addWidget(m_pages);

    connect(m_picker, &CoinPicker::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    showWalletCoins(m_picker, m_vault, false);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::contactsChanged}) {
        connect(m_vault, signal, this, &CoinContactsSwitcher::refresh);
    }
    refresh();
}

QWidget *CoinContactsSwitcher::makePage(const CoinParams &params, QTreeWidget *tree, QLabel *status) {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    tree->setParent(page);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setHeaderLabels({"Name", "Address"});
    tree->header()->setSectionResizeMode(ColAddress, QHeaderView::Stretch);
    tree->setSortingEnabled(true);
    tree->sortByColumn(ColName, Qt::AscendingOrder);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tree);

    auto *bottom = new QHBoxLayout;
    status->setParent(page);
    status->setStyleSheet("color: gray;");
    bottom->addWidget(status, 1);
    auto *add = new QPushButton("Add contact", page);
    add->setAutoDefault(false);
    bottom->addWidget(add);
    layout->addLayout(bottom);

    const CoinParams *p = &params;
    connect(add, &QPushButton::clicked, this, [this, p] { editContact(*p, -1); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        emit payTo(item->text(ColAddress), item->text(ColName));
    });
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this, tree, p](const QPoint &pos) {
        QTreeWidgetItem *item = tree->itemAt(pos);
        if (!item) {
            return;
        }
        const int index = item->data(ColName, IndexRole).toInt();
        const QString address = item->text(ColAddress);
        const QString name = item->text(ColName);
        QMenu menu(this);
        menu.addAction("Pay to", [this, address, name] { emit payTo(address, name); });
        menu.addAction("Copy address", [address] { Utils::copyToClipboard(address); });
        menu.addAction("Copy name", [name] { Utils::copyToClipboard(name); });
        menu.addSeparator();
        menu.addAction("Edit…", [this, p, index] { editContact(*p, index); });
        menu.addAction("Delete", [this, p, index] { removeContact(*p, index); });
        menu.exec(tree->viewport()->mapToGlobal(pos));
    });
    return page;
}

void CoinContactsSwitcher::refresh() {
    for (auto [tree, status, params] : {std::tuple{m_btc, m_btcStatus, &bitcoin()},
                                        std::tuple{m_ltc, m_ltcStatus, &litecoin()},
                                        std::tuple{m_eth, m_ethStatus, &ethereum()}}) {
        tree->setSortingEnabled(false);
        tree->clear();
        const bool unlocked = m_vault->isUnlocked();
        tree->setEnabled(unlocked);
        if (!unlocked) {
            status->setText(m_vault->exists() ? QString("Unlock %1 to see its contacts.").arg(params->name)
                                              : QString("%1 is not in this wallet. Add it with + in Receive.").arg(params->name));
            tree->setSortingEnabled(true);
            continue;
        }
        const auto contacts = m_vault->contacts(*params);
        for (int i = 0; i < contacts.size(); ++i) {
            auto *item = new QTreeWidgetItem(tree);
            item->setText(ColName, contacts[i].name);
            item->setText(ColAddress, contacts[i].address);
            item->setData(ColName, IndexRole, i);
        }
        tree->setSortingEnabled(true);
        status->setText(contacts.isEmpty() ? QString("No %1 contact yet.").arg(params->name)
                        : params->ethereum ? QString("Double-click a contact to pay it (ETH, USDT or USDC). Kept in the encrypted wallet file.")
                                           : QString("Double-click a contact to pay it. Kept in the encrypted wallet file."));
    }
}

void CoinContactsSwitcher::editContact(const CoinParams &params, int index) {
    if (!m_vault->isUnlocked()) {
        Utils::showError(this, "Locked", QString("Unlock %1 first (Receive or Send tab).").arg(params.name));
        return;
    }
    auto contacts = m_vault->contacts(params);
    const bool isNew = index < 0 || index >= contacts.size();

    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? QString("New %1 contact").arg(params.name) : QString("Edit %1 contact").arg(params.name));
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(isNew ? QString() : contacts[index].name, &dialog);
    auto *address = new QLineEdit(isNew ? QString() : contacts[index].address, &dialog);
    address->setMinimumWidth(420);
    address->setPlaceholderText(params.ethereum     ? "0x… (also for USDT and USDC on Ethereum)"
                                : params == bitcoin() ? "bc1…, 1…, 3… or a silent payment address (sp1…)" : "ltc1…, L…, M…");
    form->addRow("Name:", name);
    form->addRow("Address:", address);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty()) {
            Utils::showError(&dialog, "Name missing", "Give the contact a name.");
            return;
        }
        if (params.ethereum) {
            QString error;
            if (!eth::parseAddress(address->text(), &error)) {
                Utils::showError(&dialog, "Invalid address", error);
                return;
            }
        } else if (!isValidSendDestination(address->text().trimmed(), params)) {
            Utils::showError(&dialog, "Invalid address", QString("This is not a valid %1 address.").arg(params.name));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Ethereum: kept with its checksum, whatever case it was typed in.
    QString saved = address->text().trimmed();
    if (params.ethereum) {
        saved = eth::checksumAddress(*eth::parseAddress(saved));
    }
    const CoinVault::Contact contact{name->text().trimmed(), saved};
    if (isNew) {
        contacts.append(contact);
    } else {
        contacts[index] = contact;
    }
    QString error;
    if (!m_vault->setContacts(params, contacts, &error)) {
        Utils::showError(this, "Contact not saved", error);
    }
}

void CoinContactsSwitcher::removeContact(const CoinParams &params, int index) {
    auto contacts = m_vault->contacts(params);
    if (index < 0 || index >= contacts.size()) {
        return;
    }
    contacts.removeAt(index);
    QString error;
    if (!m_vault->setContacts(params, contacts, &error)) {
        Utils::showError(this, "Contact not deleted", error);
    }
}

}
