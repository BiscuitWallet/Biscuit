// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef HOMEWIDGET_H
#define HOMEWIDGET_H

#include <QPointer>
#include <QWidget>

#include "plugins/Plugin.h"

class QLabel;
class QTreeWidget;
class Wallet;

namespace Ui {
    class HomeWidget;
}

class HomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HomeWidget(Wallet *wallet, QWidget *parent = nullptr);
    ~HomeWidget();

    void addPlugin(Plugin *plugin);
    void aboutToQuit();
    void uiSetup();

private:
    // Biscuit dashboard: total of every coin, recent activity of every wallet.
    void updateTotal();
    void updateRecent();
    void showHistoryTab();

    QScopedPointer<Ui::HomeWidget> ui;
    QPointer<Wallet> m_wallet;
    QLabel *m_total = nullptr;
    QLabel *m_coins = nullptr;
    QTreeWidget *m_recent = nullptr;
    QLabel *m_recentEmpty = nullptr;
};

#endif //HOMEWIDGET_H
