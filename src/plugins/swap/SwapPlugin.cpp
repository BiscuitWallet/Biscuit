// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapPlugin.h"

#include "plugins/PluginRegistry.h"

SwapPlugin::SwapPlugin()
{
}

void SwapPlugin::initialize(Wallet *wallet, QObject *parent) {
    this->setParent(parent);
    m_tab = new SwapWidget(wallet, nullptr);
}

QString SwapPlugin::id() {
    return "swap";
}

int SwapPlugin::idx() const {
    return 40;
}

QString SwapPlugin::parent() {
    return {};
}

QString SwapPlugin::displayName() {
    return "Swap";
}

QString SwapPlugin::description() {
    return "Exchange XMR with other coins";
}

QString SwapPlugin::icon() {
    return "exchange.png";
}

QStringList SwapPlugin::socketData() {
    return {};
}

Plugin::PluginType SwapPlugin::type() {
    return Plugin::PluginType::TAB;
}

QWidget* SwapPlugin::tab() {
    return m_tab;
}

const bool SwapPlugin::registered = [] {
    PluginRegistry::registerPlugin(SwapPlugin::create());
    PluginRegistry::getInstance().registerPluginCreator(&SwapPlugin::create);
    return true;
}();
