// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ATOMICSWAPDIALOG_H
#define BISCUIT_ATOMICSWAPDIALOG_H

#include <QDialog>

#include "swap/core/AtomicEvents.h"

class QLabel;
class QLineEdit;
class QPushButton;

// Confirmation of a BTC -> XMR atomic swap: amount, what the user gets, what
// can go wrong. The swap starts only from here.
class AtomicSwapDialog : public QDialog
{
    Q_OBJECT

public:
    // balanceSat: confirmed balance of the Bitcoin wallet the BTC comes from.
    AtomicSwapDialog(const biscuit::swap::atomic::MakerOffer &offer, const QString &walletName, quint64 balanceSat,
                     bool tor, double marketBtcPerXmr, QWidget *parent = nullptr);

    quint64 btcSat() const { return m_btcSat; }

private:
    void updateAmount();

    biscuit::swap::atomic::MakerOffer m_offer;
    quint64 m_balanceSat;
    quint64 m_btcSat = 0;
    QLineEdit *m_amount;
    QLabel *m_receive;
    QLabel *m_problem;
    QPushButton *m_start;
};

#endif // BISCUIT_ATOMICSWAPDIALOG_H
