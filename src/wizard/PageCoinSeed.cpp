// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PageCoinSeed.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "WalletWizard.h"
#include "coins/CoinSetupDialog.h"
#include "coins/CoinVault.h"
#include "coins/core/Bip39.h"

namespace {
    // "Bitcoin and Litecoin", "Bitcoin"… from the coins chosen.
    QString chosenCoins(const WizardFields *fields) {
        QList<const biscuit::coins::CoinParams *> coins;
        for (const auto *params : biscuit::coins::walletCoins()) {
            if (fields->coinTickers.contains(params->ticker)) coins << params;
        }
        return biscuit::coins::coinNames(coins);
    }

    int afterCoinPages(const WizardFields *fields) {
        // Same path as after the Monero seed page.
        if (fields->showSetSeedPassphrasePage) {
            return WalletWizard::Page_SetSeedPassphrase;
        }
        return WalletWizard::Page_WalletFile;
    }
}

PageOtherCoins::PageOtherCoins(WizardFields *fields, QWidget *parent)
    : QWizardPage(parent)
    , m_fields(fields)
{
    setTitle("Other coins");
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Biscuit can also hold other coins in this wallet. This is optional: Monero works on its "
                             "own, and you can add a coin at any time from the Receive tab.\n\n"
                             "Leave everything unticked to use Monero only.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    for (const auto *params : biscuit::coins::walletCoins()) {
        auto *check = new QCheckBox(params->name, this);
        layout->addWidget(check);
        m_coins << check;
    }
    layout->addStretch();
}

void PageOtherCoins::initializePage() {
    const auto &coins = biscuit::coins::walletCoins();
    for (int i = 0; i < coins.size(); ++i) {
        m_coins.at(i)->setChecked(m_fields->coinTickers.contains(coins.at(i)->ticker));
    }
}

bool PageOtherCoins::validatePage() {
    m_fields->coinTickers.clear();
    const auto &coins = biscuit::coins::walletCoins();
    for (int i = 0; i < coins.size(); ++i) {
        if (m_coins.at(i)->isChecked()) m_fields->coinTickers << coins.at(i)->ticker;
    }
    return true;
}

int PageOtherCoins::nextId() const {
    for (const QCheckBox *check : m_coins) {
        if (check->isChecked()) {
            return WalletWizard::Page_CoinSeed;
        }
    }
    return afterCoinPages(m_fields);
}

PageCoinSeed::PageCoinSeed(WizardFields *fields, QWidget *parent)
    : QWizardPage(parent)
    , m_fields(fields)
    , m_layout(new QVBoxLayout(this))
{
    m_intro = new QLabel(this);
    m_intro->setWordWrap(true);
    m_layout->addWidget(m_intro);

    m_written = new QCheckBox("I have written down these 12 words, next to my Monero seed", this);
    connect(m_written, &QCheckBox::toggled, this, &QWizardPage::completeChanged);
    m_layout->addWidget(m_written);
    m_layout->addStretch();
}

void PageCoinSeed::initializePage() {
    const QString coins = chosenCoins(m_fields);
    setTitle(QString("%1 seed").arg(coins));
    m_intro->setText(QString("%1 uses its own 12 words, separate from your Monero seed and compatible with other "
                             "wallets. Coins you add later use the same words.\n\n"
                             "Write these 12 words on paper, in order, and keep them offline. They are the only way "
                             "to recover your %1 if this computer is lost. Anyone who has them can take your coins.")
                     .arg(coins));
    // Kept when going back and forth; a new wallet from the menu starts over.
    if (m_fields->coinMnemonic.isEmpty()) {
        m_fields->coinMnemonic = biscuit::coins::bip39::generateMnemonic(12);
        m_fields->coinCheckIndexes = biscuit::coins::randomWordIndexes(12, 3);
        m_written->setChecked(false);
        delete m_words;
        m_words = nullptr;
    }
    if (!m_words) {
        m_words = biscuit::coins::seedWordsView(m_fields->coinMnemonic, this);
        m_layout->insertWidget(1, m_words);
    }
}

bool PageCoinSeed::isComplete() const {
    return m_written->isChecked();
}

int PageCoinSeed::nextId() const {
    return WalletWizard::Page_CoinSeedVerify;
}

PageCoinSeedVerify::PageCoinSeedVerify(WizardFields *fields, QWidget *parent)
    : QWizardPage(parent)
    , m_fields(fields)
{
    setTitle("Check your seed");
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("To make sure your backup is correct, type these words from your paper.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    for (int i = 0; i < 3; ++i) {
        auto *label = new QLabel(this);
        auto *line = new QLineEdit(this);
        line->setMaximumWidth(200);
        form->addRow(label, line);
        m_labels << label;
        m_lines << line;
    }
    layout->addLayout(form);
    m_error = new QLabel(this);
    m_error->setStyleSheet("color: #c0392b;");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    layout->addStretch();
}

void PageCoinSeedVerify::initializePage() {
    setTitle(QString("Check your %1 seed").arg(chosenCoins(m_fields)));
    m_error->clear();
    for (int i = 0; i < m_lines.size(); ++i) {
        m_labels.at(i)->setText(QString("Word %1").arg(m_fields->coinCheckIndexes.value(i) + 1));
        m_lines.at(i)->clear();
    }
}

bool PageCoinSeedVerify::validatePage() {
    const QStringList words = m_fields->coinMnemonic.split(' ');
    for (int i = 0; i < m_lines.size(); ++i) {
        const int index = m_fields->coinCheckIndexes.value(i);
        if (m_lines.at(i)->text().trimmed().toLower() != words.value(index)) {
            m_error->setText(QString("Word %1 does not match. Check your paper backup.").arg(index + 1));
            m_lines.at(i)->setFocus();
            return false;
        }
    }
    return true;
}

int PageCoinSeedVerify::nextId() const {
    return afterCoinPages(m_fields);
}
