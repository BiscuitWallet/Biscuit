// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_SIGNVERIFYDIALOG_H
#define FEATHER_SIGNVERIFYDIALOG_H

#include <QDialog>

#include "components.h"
#include "libwalletqt/Wallet.h"

class QComboBox;
namespace biscuit::coins { struct CoinParams; }

namespace Ui {
    class SignVerifyDialog;
}

class SignVerifyDialog : public WindowModalDialog
{
Q_OBJECT

public:
    explicit SignVerifyDialog(Wallet *wallet, QWidget *parent = nullptr);
    ~SignVerifyDialog() override;

private slots:
    void signMessage();
    void verifyMessage();
    void copyToClipboard();

private:
    // Biscuit: Bitcoin / Litecoin messages too (the coin of the address).
    const biscuit::coins::CoinParams *coinOf(const QString &address) const;

    QScopedPointer<Ui::SignVerifyDialog> ui;
    Wallet *m_wallet;
    QComboBox *m_coin;
};


#endif //FEATHER_SIGNVERIFYDIALOG_H
