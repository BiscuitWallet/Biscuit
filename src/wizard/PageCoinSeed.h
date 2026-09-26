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

// New wallet: the Bitcoin and Litecoin seed (12 BIP39 words), right after the
// Monero seed. The BTC/LTC file is created with the wallet password once the
// Monero wallet exists (WindowManager).
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
