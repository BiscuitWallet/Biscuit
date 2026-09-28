// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_NEWSPLUGIN_H
#define BISCUIT_NEWSPLUGIN_H

#include "plugins/Plugin.h"

class NewsWidget;

// Biscuit: "News" tab of Home, fed by the public data feed (news/feed.xml on
// biscuitwallet.com), so it follows the same privacy rules as prices.
class NewsPlugin : public Plugin {
    Q_OBJECT

public:
    explicit NewsPlugin() = default;

    QString id() override;
    int idx() const override;
    QString parent() override;
    QString displayName() override;
    QString description() override;
    QString icon() override;
    QStringList socketData() override;
    PluginType type() override;
    QWidget* tab() override;

    void initialize(Wallet *wallet, QObject *parent) override;

    static NewsPlugin* create() { return new NewsPlugin(); }

private:
    NewsWidget* m_tab = nullptr;
    static const bool registered;
};

#endif // BISCUIT_NEWSPLUGIN_H
