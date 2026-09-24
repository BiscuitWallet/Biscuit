// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPTRADEDIALOG_H
#define BISCUIT_SWAPTRADEDIALOG_H

#include <QDialog>

#include "swap/SwapManager.h"

namespace Ui {
    class SwapTradeDialog;
}

// Details of one swap, updated live while the dialog is open.
class SwapTradeDialog : public QDialog
{
    Q_OBJECT

public:
    SwapTradeDialog(biscuit::swap::SwapManager *manager, const QString &providerId, const QString &tradeId,
                    QWidget *parent = nullptr);
    ~SwapTradeDialog() override;

signals:
    void sendDepositRequested(const biscuit::swap::Trade &trade);

private:
    void updateView();

    QScopedPointer<Ui::SwapTradeDialog> ui;
    biscuit::swap::SwapManager *m_manager;
    QString m_providerId;
    QString m_tradeId;
};

#endif // BISCUIT_SWAPTRADEDIALOG_H
