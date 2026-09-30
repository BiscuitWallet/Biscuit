// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINCONTACTSSWITCHER_H
#define BISCUIT_COINCONTACTSSWITCHER_H

#include <QPointer>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTreeWidget;
class Wallet;

namespace biscuit::coins {

class CoinPicker;
class CoinVault;
struct CoinParams;

// The Contacts tab with a Monero / Bitcoin / Litecoin picker: Feather's
// Monero address book, and one for Bitcoin and Litecoin kept in the
// encrypted Bitcoin/Litecoin file.
class CoinContactsSwitcher : public QWidget
{
    Q_OBJECT

public:
    CoinContactsSwitcher(Wallet *wallet, QWidget *moneroPage, QWidget *parent = nullptr);

signals:
    void payTo(const QString &address, const QString &name);

private:
    QWidget *makePage(const CoinParams &params, QTreeWidget *tree, QLabel *status);
    void refresh();
    void editContact(const CoinParams &params, int index);   // -1: new contact
    void removeContact(const CoinParams &params, int index);

    QPointer<Wallet> m_wallet;
    QPointer<CoinVault> m_vault;
    CoinPicker *m_picker;
    QStackedWidget *m_pages;
    QTreeWidget *m_btc;
    QTreeWidget *m_ltc;
    QLabel *m_btcStatus;
    QLabel *m_ltcStatus;
};

}

#endif // BISCUIT_COINCONTACTSSWITCHER_H
