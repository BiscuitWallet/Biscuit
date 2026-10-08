// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PAGECOINSEED_H
#define BISCUIT_PAGECOINSEED_H

#include <QWizardPage>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QVBoxLayout;
struct WizardFields;

// New wallet, right after the Monero seed: which other coins to hold too.
// None is the default: Monero works alone, and coins can be added later.
class PageOtherCoins : public QWizardPage
{
    Q_OBJECT

public:
    explicit PageOtherCoins(WizardFields *fields, QWidget *parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
    int nextId() const override;

private:
    WizardFields *m_fields;
    QList<QCheckBox *> m_coins;   // one per walletCoins(), same order
};

// The seed of the other coins chosen (12 BIP39 words). The BTC/LTC file is
// created with the wallet password once the Monero wallet exists (WindowManager).
class PageCoinSeed : public QWizardPage
{
    Q_OBJECT

public:
    explicit PageCoinSeed(WizardFields *fields, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
    int nextId() const override;

private:
    WizardFields *m_fields;
    QVBoxLayout *m_layout;
    QLabel *m_intro;
    QPlainTextEdit *m_words = nullptr;
    QCheckBox *m_written;
};

// Three of the words typed back, to make sure the paper backup is right.
class PageCoinSeedVerify : public QWizardPage
{
    Q_OBJECT

public:
    explicit PageCoinSeedVerify(WizardFields *fields, QWidget *parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
    int nextId() const override;

private:
    WizardFields *m_fields;
    QList<QLabel *> m_labels;
    QList<QLineEdit *> m_lines;
    QLabel *m_error;
};

#endif // BISCUIT_PAGECOINSEED_H
