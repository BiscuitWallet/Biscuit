// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPTRADEDIALOG_H
#define BISCUIT_SWAPTRADEDIALOG_H

#include <QDialog>

#include "swap/SwapManager.h"

class QLabel;
class QPushButton;

// Details of one swap, updated live while the dialog is open.
class SwapTradeDialog : public QDialog
{
    Q_OBJECT

public:
    SwapTradeDialog(biscuit::swap::SwapManager *manager, const QString &providerId, const QString &tradeId,
                    QWidget *parent = nullptr);

signals:
    void sendDepositRequested(const biscuit::swap::Trade &trade);

private:
    void updateView();

    biscuit::swap::SwapManager *m_manager;
    QString m_providerId;
    QString m_tradeId;

    QLabel *m_status;
    QLabel *m_details;
    QLabel *m_qr;
    QLabel *m_support;
    QPushButton *m_btnSend;
    QPushButton *m_btnCopyAddress;
};

#endif // BISCUIT_SWAPTRADEDIALOG_H
