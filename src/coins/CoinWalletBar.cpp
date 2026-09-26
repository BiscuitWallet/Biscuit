// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinWalletBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

#include "Amount.h"
#include "CoinParams.h"
#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "utils/Utils.h"

namespace biscuit::coins {

CoinWalletBar::CoinWalletBar(CoinVault *vault, const CoinParams &params, QWidget *parent)
    : QWidget(parent)
    , m_vault(vault)
    , m_params(params)
    , m_layout(new QHBoxLayout(this))
    , m_group(new QButtonGroup(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
    for (auto signal : {&CoinVault::unlocked, &CoinVault::locked, &CoinVault::walletsChanged}) {
        connect(m_vault, signal, this, &CoinWalletBar::rebuild);
    }
    // Balances in the tooltips.
    connect(m_vault, &CoinVault::walletUpdated, this, [this] {
        const auto entries = m_vault->wallets(m_params);
        for (QAbstractButton *b : m_group->buttons()) {
            const int i = m_group->id(b);
            if (i >= 0 && i < entries.size()) {
                b->setToolTip(QString("%1 %2").arg(swap::amount::fromAtomic(entries.at(i).wallet->balance().total(),
                                                                             m_params.decimals), m_params.ticker));
            }
        }
    });
    rebuild();
}

void CoinWalletBar::rebuild() {
    for (QAbstractButton *b : m_group->buttons()) {
        m_group->removeButton(b);
    }
    // deleteLater: the clicked button may be the one emitting this change.
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    const bool unlocked = m_vault && m_vault->isUnlocked();
    setVisible(unlocked);
    if (!unlocked) {
        return;
    }

    m_layout->addWidget(new QLabel("Wallet:", this));
    const auto entries = m_vault->wallets(m_params);
    const QString selected = m_vault->selectedId(m_params);
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        auto *button = new QPushButton(entry.name, this);
        button->setCheckable(true);
        button->setChecked(entry.id == selected);
        button->setAutoDefault(false);
        button->setToolTip(QString("%1 %2").arg(swap::amount::fromAtomic(entry.wallet->balance().total(), m_params.decimals),
                                                m_params.ticker));
        m_group->addButton(button, i);
        m_layout->addWidget(button);
        const QString id = entry.id;
        connect(button, &QPushButton::clicked, this, [this, id] { m_vault->select(m_params, id); });
        if (!entry.mainSeed) {
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QWidget::customContextMenuRequested, this, [this, button, id, name = entry.name](const QPoint &pos) {
                QMenu menu(this);
                menu.addAction(QString("Remove \"%1\"…").arg(name), [this, id, name] { confirmRemove(id, name); });
                menu.exec(button->mapToGlobal(pos));
            });
        }
    }

    auto *add = new QPushButton("Add wallet", this);
    add->setAutoDefault(false);
    add->setToolTip(QString("Add another %1 wallet").arg(m_params.name));
    auto *menu = new QMenu(add);
    menu->addAction("Restore from seed (12 or 24 words)…", this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Restore, m_params, this).exec();
    });
    menu->addAction("Create a new seed…", this, [this] {
        CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Create, m_params, this).exec();
    });
    add->setMenu(menu);
    m_layout->addSpacing(12);
    m_layout->addWidget(add);
    m_layout->addStretch();
}

void CoinWalletBar::confirmRemove(const QString &id, const QString &name) {
    QMessageBox box(this);
    box.setWindowTitle(QString("Remove %1 wallet").arg(m_params.name));
    box.setIcon(QMessageBox::Warning);
    box.setText(QString("Remove \"%1\" from Biscuit?").arg(name));
    box.setInformativeText("Its seed is erased from this computer. The coins stay on the blockchain: you need your "
                           "own backup of its words to add it again.");
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!m_vault->removeWallet(id, &error)) {
        Utils::showError(this, "Unable to remove the wallet", error);
    }
}

}
