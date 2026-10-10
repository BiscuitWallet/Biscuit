// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinSetupDialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtMath>

#include "Bip39.h"
#include "CoinParams.h"
#include "CoinVault.h"

namespace biscuit::coins {

namespace {
    QString numberedWords(const QString &mnemonic) {
        const QStringList words = mnemonic.split(' ');
        QStringList rows;
        const int perRow = 4;
        for (int i = 0; i < words.size(); i += perRow) {
            QStringList row;
            for (int j = i; j < std::min<int>(i + perRow, words.size()); ++j) {
                row << QString("%1. %2").arg(j + 1, 2).arg(words.at(j), -10);
            }
            rows << row.join("   ");
        }
        return rows.join('\n');
    }
}

QPlainTextEdit *seedWordsView(const QString &mnemonic, QWidget *parent) {
    const QString words = numberedWords(mnemonic);
    auto *view = new QPlainTextEdit(words, parent);
    view->setReadOnly(true);
    // The system's fixed-width font (Menlo is macOS only), and never wrapped:
    // the view is as wide as the longest row, so every row keeps its 4 words.
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int widest = 0;
    for (const QString &row : words.split('\n')) {
        widest = std::max(widest, view->fontMetrics().horizontalAdvance(row));
    }
    const int margins = 2 * (view->frameWidth() + qCeil(view->document()->documentMargin())) + 4;
    view->setMinimumWidth(widest + margins);
    view->setMaximumHeight(view->fontMetrics().lineSpacing() * 7);
    return view;
}

QList<int> randomWordIndexes(int wordCount, int count) {
    QList<int> indexes;
    while (indexes.size() < count) {
        const int i = QRandomGenerator::global()->bounded(wordCount);
        if (!indexes.contains(i)) indexes << i;
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

CoinSetupDialog::CoinSetupDialog(CoinVault *vault, Mode mode, const CoinParams &addTo, QWidget *parent)
    : CoinSetupDialog(vault, mode, parent, {&addTo}, &addTo)
{
}

CoinSetupDialog::CoinSetupDialog(CoinVault *vault, Mode mode, QWidget *parent, const QList<const CoinParams *> &coins,
                                 const CoinParams *addTo)
    : QDialog(parent)
    , m_vault(vault)
    , m_mode(mode)
    , m_coins(coins)
    , m_addTo(addTo)
{
    if (m_addTo) {
        setWindowTitle(mode == Mode::Create ? QString("New %1 wallet").arg(m_addTo->name)
                                            : QString("Add a %1 wallet").arg(m_addTo->name));
    } else {
        setWindowTitle(mode == Mode::Create ? QString("New %1 seed").arg(coinsText()) : QString("Restore %1").arg(coinsText()));
    }
    auto *layout = new QVBoxLayout(this);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    m_error = new QLabel(this);
    m_error->setStyleSheet("color: #c0392b;");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    m_btnNext = buttons->addButton("Next", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_btnNext, &QPushButton::clicked, this, &CoinSetupDialog::next);

    if (mode == Mode::Create) {
        m_mnemonic = bip39::generateMnemonic(12);
        m_checkIndexes = randomWordIndexes(12, 3);
        m_pages->addWidget(pageShowWords());
        m_pages->addWidget(pageVerifyWords());
    } else {
        m_pages->addWidget(pageRestore());
    }
    if (!m_addTo) {
        m_pages->addWidget(pagePassword());   // an added wallet uses the open vault
    } else if (m_pages->count() == 1) {
        m_btnNext->setText("Add");
    }
    resize(560, sizeHint().height());
}

CoinSetupDialog::~CoinSetupDialog() {
    m_mnemonic.fill(QChar(' '));
    m_mnemonic.clear();
}

QWidget *CoinSetupDialog::pageShowWords() {
    auto *page = new QWidget(this);
    auto *l = new QVBoxLayout(page);
    auto *intro = new QLabel(QString("Write these 12 words on paper, in order, and keep them offline. They are the only "
                                     "way to recover your %1 if this computer is lost. Anyone who has "
                                     "them can take your coins.").arg(coinsText()), page);
    intro->setWordWrap(true);
    l->addWidget(intro);
    if (m_addTo) addNameField(page, l);
    l->addWidget(seedWordsView(m_mnemonic, page));
    QString noteText = m_addTo ? QString("This seed is separate from your other seeds: back it up too.")
                               : QString("This seed is separate from your Monero seed: back up both.");
    QList<const CoinParams *> later;
    for (const CoinParams *params : walletCoins()) {
        if (!m_addTo && !m_coins.contains(params)) later << params;
    }
    if (!later.isEmpty()) {
        noteText += QString(" If you add %1 later, it will use these same words.").arg(coinNames(later));
    }
    auto *note = new QLabel(noteText, page);
    note->setWordWrap(true);
    l->addWidget(note);
    m_checkWritten = new QCheckBox("I have written down these 12 words", page);
    l->addWidget(m_checkWritten);
    return page;
}

QWidget *CoinSetupDialog::pageVerifyWords() {
    auto *page = new QWidget(this);
    auto *l = new QVBoxLayout(page);
    auto *intro = new QLabel("To make sure your backup is correct, type these words from your paper.", page);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *form = new QFormLayout;
    for (int index : m_checkIndexes) {
        auto *line = new QLineEdit(page);
        line->setMaximumWidth(200);
        form->addRow(QString("Word %1").arg(index + 1), line);
        m_checkLines << line;
    }
    l->addLayout(form);
    l->addStretch();
    return page;
}

QWidget *CoinSetupDialog::pageRestore() {
    auto *page = new QWidget(this);
    auto *l = new QVBoxLayout(page);
    auto *intro = new QLabel("Enter your 12 or 24 words (BIP39). Seeds from Electrum-format, Monero or other "
                             "non-BIP39 wallets are not supported.", page);
    intro->setWordWrap(true);
    l->addWidget(intro);
    if (m_addTo) {
        auto *note = new QLabel(m_addTo->ethereum
            ? QString("The first address of this seed (m/44'/60'/0'/0/0), as in MetaMask and most Ethereum wallets.")
            : QString("Biscuit uses native SegWit addresses (%1, BIP84), like most recent wallets. "
                      "Coins on older address types of this seed are not shown.")
                  .arg(*m_addTo == coins::bitcoin() ? "bc1q…" : "ltc1q…"), page);
        note->setWordWrap(true);
        note->setStyleSheet("color: gray;");
        l->addWidget(note);
        addNameField(page, l);
    }
    m_restorePage = page;
    m_restoreWords = new QPlainTextEdit(page);
    m_restoreWords->setPlaceholderText("word1 word2 word3 …");
    m_restoreWords->setMaximumHeight(m_restoreWords->fontMetrics().lineSpacing() * 5);
    l->addWidget(m_restoreWords);
    auto *form = new QFormLayout;
    m_passphrase = new QLineEdit(page);
    m_passphrase->setPlaceholderText("Only if you used one (\"25th word\")");
    form->addRow("Passphrase", m_passphrase);
    l->addLayout(form);
    l->addStretch();
    return page;
}

void CoinSetupDialog::addNameField(QWidget *page, QVBoxLayout *layout) {
    auto *form = new QFormLayout;
    m_name = new QLineEdit(m_vault->nextName(*m_addTo), page);
    m_name->setMaximumWidth(260);
    form->addRow("Name", m_name);
    layout->addLayout(form);
}

QString CoinSetupDialog::coinsText() const {
    return m_addTo ? m_addTo->name : coinNames(m_coins);
}

QWidget *CoinSetupDialog::pagePassword() {
    auto *page = new QWidget(this);
    auto *l = new QVBoxLayout(page);
    auto *intro = new QLabel(QString("Enter the password of this wallet. Your %1 seed is encrypted with it.").arg(coinsText()), page);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *form = new QFormLayout;
    m_password = new QLineEdit(page);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMaximumWidth(320);
    form->addRow("Password", m_password);
    l->addLayout(form);
    l->addStretch();
    return page;
}

void CoinSetupDialog::next() {
    m_error->clear();
    QWidget *current = m_pages->currentWidget();
    const bool lastPage = m_pages->currentIndex() == m_pages->count() - 1;

    if (m_mode == Mode::Create && m_pages->currentIndex() == 0) {
        if (!m_checkWritten->isChecked()) {
            m_error->setText("Write down the 12 words first.");
            return;
        }
    } else if (m_mode == Mode::Create && m_pages->currentIndex() == 1) {
        const QStringList words = m_mnemonic.split(' ');
        for (int i = 0; i < m_checkIndexes.size(); ++i) {
            if (m_checkLines.at(i)->text().trimmed().toLower() != words.at(m_checkIndexes.at(i))) {
                m_error->setText(QString("Word %1 does not match. Check your paper backup.").arg(m_checkIndexes.at(i) + 1));
                return;
            }
        }
    } else if (m_mode == Mode::Restore && current == m_restorePage) {
        const QString words = m_restoreWords->toPlainText();
        if (!bip39::isValidMnemonic(words)) {
            m_error->setText("These words are not a valid BIP39 seed (check spelling, order and the number of words).");
            return;
        }
        m_mnemonic = bip39::normalizeMnemonic(words);
    }

    if (lastPage) {
        finish();
        return;
    }
    m_pages->setCurrentIndex(m_pages->currentIndex() + 1);
    if (m_pages->currentIndex() == m_pages->count() - 1) {
        if (m_addTo) {
            m_btnNext->setText("Add");
        } else {
            m_btnNext->setText(m_mode == Mode::Create ? "Create" : "Restore");
            m_password->setFocus();
        }
    }
}

void CoinSetupDialog::finish() {
    m_btnNext->setEnabled(false);
    m_error->setText("Encrypting…");
    QCoreApplication::processEvents();

    QString error;
    const QString passphrase = m_passphrase ? m_passphrase->text() : QString();
    bool ok = false;
    if (m_addTo) {
        ok = m_vault->addWallet(*m_addTo, m_name->text(), m_mnemonic, passphrase, &error);
    } else {
        ok = m_vault->setUp(m_mnemonic, passphrase, m_password->text(), m_coins, &error);
        m_password->clear();
    }
    m_btnNext->setEnabled(true);
    if (!ok) {
        m_error->setText(error);
        return;
    }
    accept();
}

bool addCoinToWallet(CoinVault *vault, const CoinParams &params, QWidget *parent) {
    if (!vault) {
        return false;
    }
    if (!vault->exists()) {
        QMessageBox box(parent);
        box.setWindowTitle(QString("Add %1").arg(params.name));
        box.setIcon(QMessageBox::Information);
        box.setText(QString("Add %1 to this wallet?").arg(params.name));
        box.setInformativeText(QString("%1 uses its own 12 words (BIP39), separate from your Monero seed and compatible "
                                       "with other %1 wallets. Create a new seed, or restore one you already have.")
                               .arg(params.name));
        auto *create = box.addButton("Create new seed", QMessageBox::AcceptRole);
        auto *restore = box.addButton("Restore from seed", QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != create && box.clickedButton() != restore) {
            return false;
        }
        CoinSetupDialog(vault, box.clickedButton() == create ? CoinSetupDialog::Mode::Create : CoinSetupDialog::Mode::Restore,
                        QList<const CoinParams *>{&params}, parent).exec();
        return vault->hasCoin(params);
    }
    if (!vault->isUnlocked()) {
        bool ok = false;
        const QString password = QInputDialog::getText(parent, params.name, "Password of this wallet:",
                                                       QLineEdit::Password, {}, &ok);
        if (!ok) {
            return false;
        }
        QString error;
        if (!vault->unlock(password, &error)) {
            QMessageBox::warning(parent, params.name, error);
            return false;
        }
    }
    if (vault->hasCoin(params)) {
        return true;
    }
    QMessageBox box(parent);
    box.setWindowTitle(QString("Add %1").arg(params.name));
    box.setIcon(QMessageBox::Question);
    box.setText(QString("Add %1 to this wallet?").arg(params.name));
    const QString mainCoins = vault->mainSeedCoinsText();
    box.setInformativeText(mainCoins.isEmpty()
        ? QString("It uses the 12 words written down when the first coin was added to this wallet.")
        : QString("It uses the same 12 words as your %1, so your paper backup already covers it.").arg(mainCoins));
    auto *add = box.addButton(QString("Add %1").arg(params.name), QMessageBox::AcceptRole);
    // To check the paper backup before relying on it (asks the password).
    auto *show = box.addButton("Show the 12 words…", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(add);
    for (box.exec(); box.clickedButton() == show; box.exec()) {
        showCoinSeed(vault, parent);
    }
    if (box.clickedButton() != add) {
        return false;
    }
    QString error;
    if (!vault->addCoin(params, &error)) {
        QMessageBox::warning(parent, QString("Add %1").arg(params.name), error);
        return false;
    }
    return true;
}

QString assetLabel(const QString &ticker) {
    const CoinParams *params = coinOfTicker(ticker);
    if (!params) {
        return ticker;
    }
    return params->ticker == ticker ? params->name : QString("%1 (on Ethereum)").arg(ticker);
}

bool addAssetToWallet(CoinVault *vault, const QString &ticker, QWidget *parent) {
    const CoinParams *params = coinOfTicker(ticker);
    if (!vault || !params) {
        return false;
    }
    if (params->ticker == ticker) {
        return addCoinToWallet(vault, *params, parent);
    }
    // A token: open the wallet first if it is locked, to know what is in it.
    if (vault->exists() && !vault->isUnlocked()) {
        bool ok = false;
        const QString password = QInputDialog::getText(parent, ticker, "Password of this wallet:", QLineEdit::Password, {}, &ok);
        QString error;
        if (!ok) {
            return false;
        }
        if (!vault->unlock(password, &error)) {
            QMessageBox::warning(parent, ticker, error);
            return false;
        }
    }
    if (vault->assetState(ticker) == CoinVault::CoinState::Ready) {
        return true;
    }
    // What a token on Ethereum means, before it is added: always said.
    const bool withEthereum = !vault->hasCoin(ethereum());
    QMessageBox box(parent);
    box.setWindowTitle(QString("Add %1").arg(ticker));
    box.setIcon(QMessageBox::Information);
    box.setText(withEthereum ? QString("Add %1? It runs on Ethereum, which is added with it.").arg(ticker)
                             : QString("Add %1 to this wallet?").arg(ticker));
    box.setInformativeText(QString(
        "%1 here is %1 on the Ethereum network (ERC-20), on the same address as your Ethereum wallet.\n\n"
        "Network fees are paid in ETH, not in %1: to send %1, this address needs a little ETH. A send usually "
        "costs a few cents (more when the network is busy), so a few dollars of ETH cover many sends.\n\n"
        "Only receive %1 sent on the Ethereum network: %1 sent on Tron, BNB Chain or any other network is lost.")
        .arg(ticker));
    auto *add = box.addButton(QString("Add %1").arg(ticker), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(add);
    box.exec();
    if (box.clickedButton() != add) {
        return false;
    }
    if (withEthereum && !addCoinToWallet(vault, ethereum(), parent)) {
        return false;
    }
    QString error;
    if (!vault->setTokenEnabled(ticker, true, &error)) {
        QMessageBox::warning(parent, QString("Add %1").arg(ticker), error);
        return false;
    }
    return true;
}

void showCoinSeed(CoinVault *vault, QWidget *parent, const QString &id) {
    // Which seed: the main one (Bitcoin and Litecoin), or an added wallet (one coin).
    const CoinParams *coin = nullptr;
    QString name;
    if (!id.isEmpty()) {
        for (const CoinParams *params : walletCoins()) {
            for (const auto &entry : vault->wallets(*params)) {
                if (entry.id == id && !entry.mainSeed) {
                    coin = params;
                    name = entry.name;
                }
            }
        }
    }

    bool ok = false;
    const QString password = QInputDialog::getText(parent, "Show seed", "Password of this wallet:",
                                                   QLineEdit::Password, {}, &ok);
    if (!ok) {
        return;
    }
    QString error;
    auto seed = vault->revealMnemonic(password, &error, coin ? id : QString());
    if (!seed) {
        QMessageBox::warning(parent, "Show seed", error);
        return;
    }

    QDialog dialog(parent);
    // No coin left on the main seed (only added wallets): still shown, as the shared seed.
    const QString mainCoins = vault->mainSeedCoinsText().isEmpty() ? QString("coins") : vault->mainSeedCoinsText();
    dialog.setWindowTitle(coin ? QString("Seed of \"%1\"").arg(name)
                               : vault->mainSeedCoinsText().isEmpty() ? QString("Shared seed") : QString("%1 seed").arg(mainCoins));
    auto *l = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(coin ? QString("Never share these words. Anyone who has them can take the %1 in \"%2\".").arg(coin->name, name)
                                  : QString("Never share these words. Anyone who has them can take your %1.").arg(mainCoins), &dialog);
    intro->setWordWrap(true);
    l->addWidget(intro);
    l->addWidget(seedWordsView(seed->first, &dialog));
    if (!seed->second.isEmpty()) {
        l->addWidget(new QLabel("This seed also uses a passphrase.", &dialog));
    }
    QStringList pathText{"BIP39"};
    for (const CoinParams *params : coin ? QList<const CoinParams *>{coin} : vault->mainSeedCoins()) {
        // Bitcoin and Litecoin: native SegWit (BIP84). Ethereum: as MetaMask.
        pathText << (params->ethereum ? QString("Ethereum m/44'/60'/0'/0/0")
                                      : QString("%1 m/84'/%2'/0' (BIP84)").arg(params->name).arg(params->bip44CoinType));
    }
    auto *paths = new QLabel(pathText.join(" · "), &dialog);
    paths->setStyleSheet("color: gray;");
    l->addWidget(paths);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    l->addWidget(buttons);
    dialog.resize(560, dialog.sizeHint().height());
    dialog.exec();
    seed->first.fill(QChar(' '));
}


void addCoinSeedActions(QMenu *menu, CoinVault *vault, QWidget *parent) {
    const QString mainCoins = vault->mainSeedCoinsText();
    menu->addAction(mainCoins.isEmpty() ? QString("Shared seed (no coin uses it now)…") : QString("%1 seed…").arg(mainCoins),
                    parent, [vault, parent] { showCoinSeed(vault, parent); });
    for (const CoinParams *params : walletCoins()) {
        for (const auto &entry : vault->wallets(*params)) {
            if (entry.mainSeed) {
                continue;
            }
            const QString id = entry.id;
            menu->addAction(QString("%1 seed (%2)…").arg(entry.name, params->name), parent,
                            [vault, parent, id] { showCoinSeed(vault, parent, id); });
        }
    }
}

}
