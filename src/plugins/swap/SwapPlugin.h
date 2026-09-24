// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPPLUGIN_H
#define BISCUIT_SWAPPLUGIN_H

#include "plugins/Plugin.h"
#include "SwapWidget.h"

class SwapPlugin : public Plugin {
    Q_OBJECT

public:
    explicit SwapPlugin();

    QString id() override;
    int idx() const override;
    QString parent() override;
    QString displayName() override;
    QString description() override;
    QString icon() override;
    QStringList socketData() override;
    PluginType type() override;
    QWidget* tab() override;
    bool requiresWebsocket() override { return false; }

    void initialize(Wallet *wallet, QObject *parent) override;

    static SwapPlugin* create() { return new SwapPlugin(); }

private:
    SwapWidget* m_tab = nullptr;
    static const bool registered;
};

#endif // BISCUIT_SWAPPLUGIN_H
