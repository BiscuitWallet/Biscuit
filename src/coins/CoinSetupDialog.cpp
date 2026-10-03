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
    : CoinSetupDialog(vault, mode, parent, &addTo)
{
}

CoinSetupDialog::CoinSetupDialog(CoinVault *vault, Mode mode, QWidget *parent, const CoinParams *addTo)
    : QDialog(parent)
    , m_vault(vault)
    , m_mode(mode)
    , m_addTo(addTo)
{
    if (m_addTo) {
        setWindowTitle(mode == Mode::Create ? QString("New %1 wallet").arg(m_addTo->name)
                                            : QString("Add a %1 wallet").arg(m_addTo->name));
    } else {
        setWindowTitle(mode == Mode::Create ? "New Bitcoin and Litecoin seed" : "Restore Bitcoin and Litecoin");
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
    auto *note = new QLabel(m_addTo ? QString("This seed is separate from your other seeds: back it up too.")
                                    : QString("This seed is separate from your Monero seed: back up both."), page);
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
        auto *note = new QLabel(QString("Biscuit uses native SegWit addresses (%1, BIP84), like most recent wallets. "
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
    return m_addTo ? m_addTo->name : QString("Bitcoin and Litecoin");
}

QWidget *CoinSetupDialog::pagePassword() {
    auto *page = new QWidget(this);
    auto *l = new QVBoxLayout(page);
    auto *intro = new QLabel("Enter the password of this wallet. Bitcoin and Litecoin are encrypted with it.", page);
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
        ok = m_vault->setUp(m_mnemonic, passphrase, m_password->text(), &error);
        m_password->clear();
    }
    m_btnNext->setEnabled(true);
    if (!ok) {
        m_error->setText(error);
        return;
    }
    accept();
}

void showCoinSeed(CoinVault *vault, QWidget *parent, const QString &id) {
    // Which seed: the main one (Bitcoin and Litecoin), or an added wallet (one coin).
    const CoinParams *coin = nullptr;
    QString name;
    if (!id.isEmpty()) {
        for (const CoinParams *params : {&bitcoin(), &litecoin()}) {
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
    dialog.setWindowTitle(coin ? QString("Seed of \"%1\"").arg(name) : QString("Bitcoin and Litecoin seed"));
    auto *l = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(coin ? QString("Never share these words. Anyone who has them can take the %1 in \"%2\".").arg(coin->name, name)
                                  : QString("Never share these words. Anyone who has them can take your Bitcoin and Litecoin."), &dialog);
    intro->setWordWrap(true);
    l->addWidget(intro);
    l->addWidget(seedWordsView(seed->first, &dialog));
    if (!seed->second.isEmpty()) {
        l->addWidget(new QLabel("This seed also uses a passphrase.", &dialog));
    }
    auto *paths = new QLabel(coin == &bitcoin()    ? QString("BIP39 · native SegWit (BIP84) · Bitcoin m/84'/0'/0'")
                             : coin == &litecoin() ? QString("BIP39 · native SegWit (BIP84) · Litecoin m/84'/2'/0'")
                             : QString("BIP39 · native SegWit (BIP84) · Bitcoin m/84'/0'/0' · Litecoin m/84'/2'/0'"), &dialog);
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
    menu->addAction("Bitcoin and Litecoin seed…", parent, [vault, parent] { showCoinSeed(vault, parent); });
    for (const CoinParams *params : {&bitcoin(), &litecoin()}) {
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
