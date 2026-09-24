// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINSETUPDIALOG_H
#define BISCUIT_COINSETUPDIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace biscuit::coins {

class CoinVault;

// Sets up the Bitcoin/Litecoin seed of a wallet:
//  - create: shows 12 new words, asks for 3 of them to make sure they were
//    written down, then the wallet password;
//  - restore: words (+ optional BIP39 passphrase), then the wallet password.
class CoinSetupDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Create, Restore };
    CoinSetupDialog(CoinVault *vault, Mode mode, QWidget *parent = nullptr);
    ~CoinSetupDialog() override;

private:
    void next();
    void finish();
    QWidget *pageShowWords();
    QWidget *pageVerifyWords();
    QWidget *pageRestore();
    QWidget *pagePassword();

    CoinVault *m_vault;
    Mode m_mode;
    QString m_mnemonic;           // wiped on destruction
    QList<int> m_checkIndexes;    // words asked back (0-based)

    QStackedWidget *m_pages;
    QPushButton *m_btnNext;
    QCheckBox *m_checkWritten = nullptr;
    QList<QLineEdit *> m_checkLines;
    QPlainTextEdit *m_restoreWords = nullptr;
    QLineEdit *m_passphrase = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_error = nullptr;
};

// Shows the Bitcoin/Litecoin seed after asking the wallet password again.
void showCoinSeed(CoinVault *vault, QWidget *parent);

}

#endif // BISCUIT_COINSETUPDIALOG_H
