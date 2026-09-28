// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "NewsPlugin.h"

#include "plugins/PluginRegistry.h"
#include "NewsWidget.h"

void NewsPlugin::initialize(Wallet *wallet, QObject *parent) {
    Q_UNUSED(wallet)
    this->setParent(parent);
    m_tab = new NewsWidget(nullptr);
}

QString NewsPlugin::id() {
    return "news";
}

int NewsPlugin::idx() const {
    return 5;   // first tab of Home, before Crowdfunding
}

QString NewsPlugin::parent() {
    return "home";
}

QString NewsPlugin::displayName() {
    return "News";
}

QString NewsPlugin::description() {
    return {};
}

QString NewsPlugin::icon() {
    return {};
}

QStringList NewsPlugin::socketData() {
    return {"news"};
}

Plugin::PluginType NewsPlugin::type() {
    return Plugin::PluginType::TAB;
}

QWidget* NewsPlugin::tab() {
    return m_tab;
}

const bool NewsPlugin::registered = [] {
    PluginRegistry::registerPlugin(NewsPlugin::create());
    PluginRegistry::getInstance().registerPluginCreator(&NewsPlugin::create);
    return true;
}();
