// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINSPLUGIN_H
#define BISCUIT_COINSPLUGIN_H

#include "plugins/Plugin.h"
#include "coins/CoinTab.h"

// One tab per coin ("Bitcoin", "Litecoin"), sharing the wallet's CoinVault.
class CoinPlugin : public Plugin {
    Q_OBJECT

public:
    CoinPlugin(QString id, QString name, QString icon, int idx, const biscuit::coins::CoinParams &params)
        : m_id(std::move(id)), m_name(std::move(name)), m_icon(std::move(icon)), m_idx(idx), m_params(params) {}

    QString id() override { return m_id; }
    int idx() const override { return m_idx; }
    QString parent() override { return {}; }
    QString displayName() override { return m_name; }
    QString description() override { return QString("%1 wallet").arg(m_name); }
    QString icon() override { return m_icon; }
    QStringList socketData() override { return {}; }
    PluginType type() override { return Plugin::PluginType::TAB; }
    QWidget *tab() override { return m_tab; }
    bool requiresWebsocket() override { return false; }

    void initialize(Wallet *wallet, QObject *parent) override {
        this->setParent(parent);
        m_tab = new biscuit::coins::CoinTab(wallet, m_params, nullptr);
    }

private:
    QString m_id, m_name, m_icon;
    int m_idx;
    const biscuit::coins::CoinParams &m_params;
    biscuit::coins::CoinTab *m_tab = nullptr;
};

#endif // BISCUIT_COINSPLUGIN_H
