// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageNetworkProxy.h"
#include "ui_PageNetworkProxy.h"

#include <QTimer>

#include "WalletWizard.h"

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
}

int PageNetworkProxy::nextId() const {
#ifdef WITH_FEATHER_SERVICES
    return WalletWizard::Page_NetworkWebsocket;
#else
    return WalletWizard::Page_Menu;
#endif
}

bool PageNetworkProxy::validatePage() {
    if (ui->proxyWidget->isProxySettingsChanged()) {
        ui->proxyWidget->setProxySettings();
    }

    emit initialNetworkConfigured();
    return true;
}