// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageNetworkProxy.h"
#include "ui_PageNetworkProxy.h"

#include <QTimer>

#include "WalletWizard.h"
#include "utils/config.h"
#include "utils/Utils.h"

PageNetworkProxy::PageNetworkProxy(QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::PageNetworkProxy)
{
    ui->setupUi(this);

    connect(ui->radio_configureManually, &QRadioButton::toggled, [this](bool checked){
        ui->frame_privacyLevel->setVisible(checked);
        this->adjustSize();
        this->updateGeometry();
    });

    ui->proxyWidget->setDisableTorLogs();
}

void PageNetworkProxy::initializePage() {
    // Fuck you Qt. No squish.
    QTimer::singleShot(1, [this]{
        ui->frame_privacyLevel->setVisible(false);
    });

    // Biscuit: in Tor mode, a Tor already running on this computer (the SOCKS
    // address in the settings, 127.0.0.1:9050 by default) is used instead of
    // the bundled one. Someone running their own Tor most likely wants it:
    // suggest Tor and say which one.
    const QString host = conf()->get(Config::socks5Host).toString();
    const quint16 port = conf()->get(Config::socks5Port).toString().toUShort();
    if (Utils::portOpen(host, port)) {
        ui->radio_tor->setChecked(true);
        ui->label_torSwaps->setText(QString("A Tor is already running on this computer (%1:%2): Biscuit will use it, "
                                            "and your IP address stays hidden. Exchange swaps are off, because exchanges "
                                            "refuse Tor connections. Atomic swaps still work, through Tor.").arg(host).arg(port));
    }
}

int PageNetworkProxy::nextId() const {
    return WalletWizard::Page_NetworkWebsocket;
}

bool PageNetworkProxy::validatePage() {
    if (ui->radio_tor->isChecked()) {
        // Biscuit: one-click Tor, managed by the app when it ships a Tor binary.
        conf()->set(Config::proxy, Config::Proxy::Tor);
        conf()->set(Config::torPrivacyLevel, Config::allTor);
#if defined(HAS_TOR_BIN) || defined(TOR_INSTALLED)
        conf()->set(Config::useLocalTor, false);
#endif
    } else if (ui->radio_useDefaultSettings->isChecked()) {
        conf()->set(Config::proxy, Config::Proxy::None);
    } else if (ui->proxyWidget->isProxySettingsChanged()) {
        ui->proxyWidget->setProxySettings();
    }

    emit initialNetworkConfigured();
    return true;
}