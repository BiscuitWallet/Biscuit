// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageNetworkProxy.h"
#include "ui_PageNetworkProxy.h"

#include <QTimer>

#include "WalletWizard.h"
#include "utils/config.h"

PageNetworkProxy::PageNetworkProxy(QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::PageNetworkProxy)
{
    ui->setupUi(this);

    // Biscuit: last page of the first-run network setup (prices and news
    // are on by default, and can be turned off in the settings).
    this->setCommitPage(true);
    this->setButtonText(QWizard::CommitButton, "Next");

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
}

int PageNetworkProxy::nextId() const {
    return WalletWizard::Page_Menu;
}

bool PageNetworkProxy::validatePage() {
    if (ui->radio_tor->isChecked()) {
        // Biscuit: one-click Tor, managed by the app when it ships a Tor binary.
        conf()->set(Config::proxy, Config::Proxy::Tor);
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