// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinWalletBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
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
    connect(m_vault, &CoinVault::walletUpdated, this, &CoinWalletBar::updateBalances);
    rebuild();
}

QString CoinWalletBar::format(quint64 amount) const {
    return QString("%1 %2").arg(swap::amount::fromAtomic(amount, m_params.decimals), m_params.ticker);
}

namespace {
    QString ethereumText(const EthWallet *eth, const QString &asset) {
        return QString("%1 %2").arg(eth::formatAmount(eth->balance(asset), EthWallet::decimals(asset)), asset);
    }

    // An Ethereum wallet: the asset of the tab ("ETH", "USDT"…).
    QString entryText(const CoinVault::Entry &entry, const CoinParams &params, const QString &asset = "ETH") {
        return entry.eth ? ethereumText(entry.eth, asset)
                         : QString("%1 %2").arg(swap::amount::fromAtomic(entry.wallet->balance().total(), params.decimals), params.ticker);
    }
}

// On the right, the selected wallet's balance in detail (pending incoming
// coins, synchronization); each button gives its balance as a tooltip.
void CoinWalletBar::updateBalances() {
    if (!m_vault || !m_vault->isUnlocked() || !m_balance) {
        return;
    }
    const auto entries = m_vault->wallets(m_params);
    for (QAbstractButton *b : m_group->buttons()) {
        const int i = m_group->id(b);
        if (i >= 0 && i < entries.size()) {
            // The name only: the selected wallet's balance is on the right.
            b->setToolTip(entryText(entries.at(i), m_params, m_asset));
        }
    }
    if (m_params.ethereum) {
        const EthWallet *eth = m_vault->ethereum();
        QString text = eth ? QString("Balance: <b>%1</b>").arg(ethereumText(eth, m_asset)) : QString();
        if (eth && eth->status() != EthWallet::Status::Synchronized) {
            text += " · <span style=\"color: gray;\">Synchronizing…</span>";
        }
        m_balance->setText(text);
        return;
    }
    CoinWallet *selected = m_vault->wallet(m_params);
    if (!selected) {
        m_balance->clear();
        return;
    }
    const auto balance = selected->balance();
    QString text = QString("Balance: <b>%1</b>").arg(format(balance.total()));
    if (balance.unconfirmed > 0) {
        text += QString(" (%1 unconfirmed)").arg(format(balance.unconfirmed));
    }
    if (selected->status() != CoinWallet::Status::Synchronized) {
        text += " · <span style=\"color: gray;\">Synchronizing…</span>";
    }
    m_balance->setText(text);
}

void CoinWalletBar::setTitle(const QString &title) {
    m_title = title;
    rebuild();
}

void CoinWalletBar::setAddVisible(bool visible) {
    m_addVisible = visible;
    rebuild();
}

void CoinWalletBar::setAsset(const QString &asset) {
    if (asset != m_asset) {
        m_asset = asset;
        updateBalances();
    }
}

void CoinWalletBar::setActive(bool active) {
    m_active = active;
    setVisible(active && m_vault && m_vault->isUnlocked());
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
    setVisible(unlocked && m_active);
    if (!unlocked) {
        return;
    }

    if (!m_title.isEmpty()) {
        m_layout->addWidget(new QLabel(m_title, this));
    }
    const auto entries = m_vault->wallets(m_params);
    const QString selected = m_vault->selectedId(m_params);
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        auto *button = new QPushButton(entry.name, this);
        button->setCheckable(true);
        button->setChecked(entry.id == selected);
        button->setAutoDefault(false);
        button->setToolTip(entryText(entry, m_params, m_asset));
        m_group->addButton(button, i);
        m_layout->addWidget(button);
        const QString id = entry.id;
        connect(button, &QPushButton::clicked, this, [this, id] { m_vault->select(m_params, id); });
        // Right click: its seed, and removing the wallets the user added.
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, button, id, name = entry.name, mainSeed = entry.mainSeed](const QPoint &pos) {
            QMenu menu(this);
            // Its address, to pay it from another wallet without going to Receive.
            menu.addAction("Copy address", [this, id] {
                for (const auto &e : m_vault->wallets(m_params)) {
                    if (e.id != id) continue;
                    const QString address = e.eth ? e.eth->addressText() : e.wallet->receiveAddress();
                    if (!address.isEmpty()) Utils::copyToClipboard(address);
                }
            });
            menu.addSeparator();
            menu.addAction("Rename…", [this, id, name] { rename(id, name); });
            menu.addAction("Show seed…", [this, id, mainSeed] { showCoinSeed(m_vault, this, mainSeed ? QString() : id); });
            if (m_params.ethereum && m_asset != "ETH") {
                // A token tab: hides the token, the Ethereum wallet stays.
                menu.addAction(QString("Remove %1 from Biscuit…").arg(m_asset), [this] { confirmRemoveToken(); });
            } else {
                menu.addAction(QString("Remove \"%1\"…").arg(name), [this, id, name, mainSeed] { confirmRemove(id, name, mainSeed); });
            }
            menu.exec(button->mapToGlobal(pos));
        });
    }

    if (m_addVisible) {
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
    }
    m_layout->addStretch();
    m_balance = new QLabel(this);
    m_balance->setTextFormat(Qt::RichText);
    m_layout->addWidget(m_balance);
    updateBalances();
}

void CoinWalletBar::rename(const QString &id, const QString &name) {
    bool ok = false;
    const QString newName = QInputDialog::getText(this, QString("Rename %1 wallet").arg(m_params.name), "New name:",
                                                  QLineEdit::Normal, name, &ok);
    if (!ok || newName.simplified() == name) {
        return;
    }
    QString error;
    if (!m_vault->renameWallet(id, newName, &error)) {
        QMessageBox::warning(this, QString("Rename %1 wallet").arg(m_params.name), error);
    }
}

void CoinWalletBar::confirmRemoveToken() {
    const eth::u128 held = m_vault->ethereumBalance(m_asset);
    QString text = QString("%1 is no longer shown in this wallet. The Ethereum wallets stay, and so does any %1 on "
                           "their addresses: add %1 again with + to see it.").arg(m_asset);
    if (held > 0) {
        text = QString("Your Ethereum wallets hold %1 %2. ").arg(eth::formatAmount(held, EthWallet::decimals(m_asset)), m_asset) + text;
    }
    QMessageBox box(this);
    box.setWindowTitle(QString("Remove %1").arg(m_asset));
    box.setIcon(QMessageBox::Question);
    box.setText(QString("Remove %1 from Biscuit?").arg(m_asset));
    box.setInformativeText(text);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!m_vault->setTokenEnabled(m_asset, false, &error)) {
        Utils::showError(this, QString("Unable to remove %1").arg(m_asset), error);
    }
}

void CoinWalletBar::confirmRemove(const QString &id, const QString &name, bool mainSeed) {
    // What is in it, and what removing it does to the words.
    bool holdsCoins = false;
    QString balance;
    int walletCount = 0;
    for (const CoinParams *params : walletCoins()) {
        for (const auto &entry : m_vault->wallets(*params)) {
            ++walletCount;
            if (entry.id != id) {
                continue;
            }
            balance = entryText(entry, *params);
            if (entry.eth) {
                QStringList held;   // every asset there is some of
                for (const QString &asset : EthWallet::assets()) {
                    if (entry.eth->balance(asset) > 0) held << ethereumText(entry.eth, asset);
                }
                holdsCoins = !held.isEmpty();
                balance = held.join(", ");
            } else {
                holdsCoins = entry.wallet->balance().total() > 0;
            }
        }
    }
    QList<const CoinParams *> sharing;
    for (const CoinParams *params : m_vault->mainSeedCoins()) {
        if (!(*params == m_params)) sharing << params;
    }

    QString text;
    if (walletCount == 1) {
        text = "Only Monero is left after this: the file holding these words is erased from this computer. "
               "Keep your paper backup of the 12 words to add the coins again.";
    } else if (mainSeed && !sharing.isEmpty()) {
        text = QString("%1 leaves this wallet. Its 12 words stay, because %2 %3 them: %1 can be added again "
                       "at any time, with the same words.")
               .arg(m_params.name, coinNames(sharing), sharing.size() > 1 ? "use" : "uses");
    } else if (mainSeed) {
        text = QString("%1 leaves this wallet. Keep your paper backup of its 12 words to add it again.").arg(m_params.name);
    } else {
        text = "Its seed is erased from this computer. You need your own backup of its words to add it again.";
    }
    if (holdsCoins) {
        text = QString("This wallet holds %1. The coins stay on the blockchain, but only the 12 words "
                       "can bring them back.\n\n").arg(balance) + text;
    }

    QMessageBox box(this);
    box.setWindowTitle(QString("Remove %1 wallet").arg(m_params.name));
    box.setIcon(QMessageBox::Warning);
    box.setText(QString("Remove the %1 wallet \"%2\" from Biscuit?").arg(m_params.name, name));
    box.setInformativeText(text);
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
