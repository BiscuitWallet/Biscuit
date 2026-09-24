// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "NetworkProxyWidget.h"
#include "ui_NetworkProxyWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QWidget>

#include "utils/config.h"
#include "utils/Utils.h"

NetworkProxyWidget::NetworkProxyWidget(QWidget *parent)
        : QWidget(parent)
        , ui(new Ui::NetworkProxyWidget)
        , m_torInfoDialog(new TorInfoDialog(this))
{
    ui->setupUi(this);

    ui->comboBox_proxy->setCurrentIndex(conf()->get(Config::proxy).toInt());
    connect(ui->comboBox_proxy, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){
        this->onProxySettingsChanged();
        ui->frame_proxy->setVisible(index != Config::Proxy::None);
        ui->groupBox_proxySettings->setTitle(QString("%1 settings").arg(ui->comboBox_proxy->currentText()));
        ui->frame_tor->setVisible(index == Config::Proxy::Tor);
        this->updatePort();
        this->updateHostVisibility();
    });

    int proxy = conf()->get(Config::proxy).toInt();
    ui->frame_proxy->setVisible(proxy != Config::Proxy::None);
    ui->frame_tor->setVisible(proxy == Config::Proxy::Tor);
    ui->groupBox_proxySettings->setTitle(QString("%1 settings").arg(ui->comboBox_proxy->currentText()));

    // [Host]
    ui->line_host->setText(conf()->get(Config::socks5Host).toString());
    connect(ui->line_host, &QLineEdit::textChanged, this, &NetworkProxyWidget::onProxySettingsChanged);

    // [Port]
    auto *portValidator = new QRegularExpressionValidator{QRegularExpression("[0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]"), this};
    ui->line_port->setValidator(portValidator);
    ui->line_port->setText(conf()->get(Config::socks5Port).toString());
    connect(ui->line_port, &QLineEdit::textChanged, this, &NetworkProxyWidget::onProxySettingsChanged);

    // [Tor settings]
    // [Let Feather start and manage a Tor daemon]
#if !defined(HAS_TOR_BIN) && !defined(TOR_INSTALLED)
    ui->checkBox_torManaged->setChecked(false);
    ui->checkBox_torManaged->setEnabled(false);
    ui->checkBox_torManaged->setToolTip("Biscuit was bundled without Tor");
#else
    ui->checkBox_torManaged->setChecked(!conf()->get(Config::useLocalTor).toBool());
    connect(ui->checkBox_torManaged, &QCheckBox::toggled, [this](bool toggled){
        this->updatePort();
        if (!toggled) {
            this->detectLocalTor();
        }
        this->updateHostVisibility();
        this->onProxySettingsChanged();
        if (!m_disableTorLogs) {
            ui->frame_torShowLogs->setVisible(toggled);
        }
    });
#endif

    // [Only allow connections to onion services]
    ui->checkBox_torOnlyAllowOnion->setChecked(conf()->get(Config::torOnlyAllowOnion).toBool());
    connect(ui->checkBox_torOnlyAllowOnion, &QCheckBox::toggled, this, &NetworkProxyWidget::onProxySettingsChanged);

    // [Node traffic]
    ui->comboBox_torNodeTraffic->setCurrentIndex(conf()->get(Config::torPrivacyLevel).toInt());
    connect(ui->comboBox_torNodeTraffic, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NetworkProxyWidget::onProxySettingsChanged);

    // [Show Tor logs]
    ui->frame_torShowLogs->setVisible(!conf()->get(Config::useLocalTor).toBool());
#if !defined(HAS_TOR_BIN) && !defined(TOR_INSTALLED)
    ui->frame_torShowLogs->setVisible(false);
#endif
    connect(ui->btn_torShowLogs, &QPushButton::clicked, [this]{
        m_torInfoDialog->show();
    });

    ui->frame_notice->hide();
    this->updateHostVisibility();
}

void NetworkProxyWidget::updateHostVisibility() {
    // Biscuit: with automatic Tor the SOCKS5 host and port are managed by the
    // app, showing them only confuses users.
    const bool managedTor = ui->comboBox_proxy->currentIndex() == Config::Proxy::Tor
                            && ui->checkBox_torManaged->isEnabled() && ui->checkBox_torManaged->isChecked();
    for (QWidget *w : {static_cast<QWidget *>(ui->label_36), static_cast<QWidget *>(ui->line_host),
                       static_cast<QWidget *>(ui->label_37), static_cast<QWidget *>(ui->line_port)}) {
        w->setVisible(!managedTor);
    }
}

void NetworkProxyWidget::detectLocalTor() {
    // "Use my own Tor": pick the port of a Tor already running on this computer,
    // the Tor service (9050) or Tor Browser (9150).
    const QString host = ui->line_host->text().isEmpty() ? QString("127.0.0.1") : ui->line_host->text();
    for (quint16 port : {9050, 9150}) {
        if (Utils::portOpen(host, port)) {
            ui->line_host->setText(host);
            ui->line_port->setText(QString::number(port));
            return;
        }
    }
}

void NetworkProxyWidget::onProxySettingsChanged() {
    if (!m_proxySettingsChanged) {
        emit proxySettingsChanged();
    }

    m_proxySettingsChanged = true;
}

void NetworkProxyWidget::updatePort() {
    int socks5port;
    switch (ui->comboBox_proxy->currentIndex()) {
        case Config::Proxy::Tor: {
            socks5port = 9050;
            break;
        }
        case Config::Proxy::i2p: {
            socks5port = 4447;
            break;
        }
        default: {
            socks5port = 9050;
        }
    }
    ui->line_port->setText(QString::number(socks5port));
}

void NetworkProxyWidget::setProxySettings() {
    conf()->set(Config::proxy, ui->comboBox_proxy->currentIndex());
    conf()->set(Config::socks5Host, ui->line_host->text());
    conf()->set(Config::socks5Port, ui->line_port->text());
    conf()->set(Config::useLocalTor, !ui->checkBox_torManaged->isChecked());
    conf()->set(Config::torOnlyAllowOnion, ui->checkBox_torOnlyAllowOnion->isChecked());
    conf()->set(Config::torPrivacyLevel, ui->comboBox_torNodeTraffic->currentIndex());
    m_proxySettingsChanged = false;
}

void NetworkProxyWidget::setDisableTorLogs() {
    m_disableTorLogs = true;
    ui->frame_torShowLogs->hide();
}

NetworkProxyWidget::~NetworkProxyWidget() = default;
