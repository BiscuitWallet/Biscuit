// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINADDRESSESDIALOG_H
#define BISCUIT_COINADDRESSESDIALOG_H

#include <QPointer>

#include "components.h"

class QComboBox;
class QLabel;
class QTreeWidget;

namespace biscuit::coins {

class CoinWallet;

// Every address of a Bitcoin or Litecoin wallet, with what it received and
// holds: earlier receive addresses keep working after a new one is shown.
class CoinAddressesDialog : public WindowModalDialog
{
    Q_OBJECT

public:
    CoinAddressesDialog(CoinWallet *coin, const QString &walletName, QWidget *parent = nullptr);

private:
    void refresh();

    QPointer<CoinWallet> m_coin;
    QComboBox *m_chain;
    QTreeWidget *m_tree;
    QLabel *m_summary;
};

}

#endif // BISCUIT_COINADDRESSESDIALOG_H
