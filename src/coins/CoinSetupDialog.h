// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINSETUPDIALOG_H
#define BISCUIT_COINSETUPDIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace biscuit::coins {

class CoinVault;
struct CoinParams;

// Seed words numbered in rows, read-only, monospace. Shared with the wizard.
QPlainTextEdit *seedWordsView(const QString &mnemonic, QWidget *parent);
// `count` distinct word positions (0-based, sorted) to type back.
QList<int> randomWordIndexes(int wordCount, int count);

// Sets up the main Bitcoin/Litecoin seed of a wallet, for `coins`:
//  - create: shows 12 new words, asks for 3 of them to make sure they were
//    written down, then the wallet password;
//  - restore: words (+ optional BIP39 passphrase), then the wallet password.
// With `addTo`, adds one more Bitcoin or Litecoin wallet with its own seed to
// an unlocked vault instead (a name, no password page).
class CoinSetupDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Create, Restore };
    CoinSetupDialog(CoinVault *vault, Mode mode, const QList<const CoinParams *> &coins, QWidget *parent = nullptr)
        : CoinSetupDialog(vault, mode, parent, coins, nullptr) {}
    CoinSetupDialog(CoinVault *vault, Mode mode, const CoinParams &addTo, QWidget *parent = nullptr);
    ~CoinSetupDialog() override;

private:
    CoinSetupDialog(CoinVault *vault, Mode mode, QWidget *parent, const QList<const CoinParams *> &coins,
                    const CoinParams *addTo);
    void next();
    void finish();
    QWidget *pageShowWords();
    QWidget *pageVerifyWords();
    QWidget *pageRestore();
    QWidget *pagePassword();
    void addNameField(QWidget *page, QVBoxLayout *layout);
    QString coinsText() const;   // "Bitcoin and Litecoin", "Bitcoin" or the added coin

    CoinVault *m_vault;
    Mode m_mode;
    QList<const CoinParams *> m_coins;   // main seed: the coins it is for
    const CoinParams *m_addTo = nullptr;
    QWidget *m_restorePage = nullptr;
    QLineEdit *m_name = nullptr;
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

// Adds a coin to the wallet, asking only what is needed: a new or restored
// seed when there is none yet, the password when locked, or a confirmation to
// use the existing seed. True once the coin is in the wallet.
bool addCoinToWallet(CoinVault *vault, const CoinParams &params, QWidget *parent);
// Same for any ticker of the coin buttons. A token (USDT, USDC) runs on
// Ethereum, whose ETH pays its fees: Ethereum is added first if needed.
bool addAssetToWallet(CoinVault *vault, const QString &ticker, QWidget *parent);
// Name in menus: "Bitcoin", "USDT (on Ethereum)"…
QString assetLabel(const QString &ticker);

// Shows a Bitcoin/Litecoin seed after asking the wallet password again: the
// main seed, or with `id` the seed of an added wallet.
void showCoinSeed(CoinVault *vault, QWidget *parent, const QString &id = {});
// Adds one "Show seed" action per Bitcoin/Litecoin seed to `menu`: the main
// seed, then each added wallet.
void addCoinSeedActions(QMenu *menu, CoinVault *vault, QWidget *parent);

}

#endif // BISCUIT_COINSETUPDIALOG_H
