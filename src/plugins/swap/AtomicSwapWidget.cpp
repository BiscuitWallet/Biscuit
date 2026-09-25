// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapWidget.h"
#include "ui_AtomicSwapWidget.h"

#include <algorithm>

#include <QHeaderView>
#include <QMessageBox>
#include <QTreeWidgetItem>

#include "utils/config.h"

using namespace biscuit::swap;

namespace {
    enum Column { Maker = 0, Price, Min, Max, Deposit };

    bool torEnabledInSettings() {
        return conf()->get(Config::proxy).toInt() != Config::Proxy::None;
    }
}

AtomicSwapWidget::AtomicSwapWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AtomicSwapWidget)
    , m_daemon(new AtomicSwapDaemon(this))
{
    ui->setupUi(this);

    QHeaderView *header = ui->tree_offers->header();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Maker, QHeaderView::Stretch);
    header->setStretchLastSection(false);

    connect(ui->btn_discover, &QPushButton::clicked, this, &AtomicSwapWidget::onDiscover);
    connect(m_daemon, &AtomicSwapDaemon::torStatusChanged, this, &AtomicSwapWidget::onTorStatus);
    connect(m_daemon, &AtomicSwapDaemon::discoveryStarted, this, [this] {
        ui->label_headline->setText("Looking for makers...");
    });
    connect(m_daemon, &AtomicSwapDaemon::summaryChanged, this, &AtomicSwapWidget::onSummary);
    connect(m_daemon, &AtomicSwapDaemon::offersChanged, this, &AtomicSwapWidget::onOffers);
    connect(m_daemon, &AtomicSwapDaemon::failed, this, &AtomicSwapWidget::onFailed);
    connect(m_daemon, &AtomicSwapDaemon::finished, this, &AtomicSwapWidget::onFinished);

    if (AtomicSwapDaemon::helperPath().isEmpty()) {
        ui->btn_discover->setEnabled(false);
        ui->label_headline->setText("The swap helper is not installed with this build.");
    }
    updateTorOption();
}

AtomicSwapWidget::~AtomicSwapWidget() = default;

void AtomicSwapWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // Tor may have been switched on or off in the settings meanwhile.
    updateTorOption();
}

void AtomicSwapWidget::updateTorOption() {
    const bool forced = torEnabledInSettings();
    if (forced) {
        ui->check_tor->setChecked(true);
        ui->check_tor->setToolTip("Tor is enabled in Biscuit's settings: all connections use Tor.");
    } else {
        ui->check_tor->setToolTip("Without Tor, rendezvous servers and makers see your IP address.");
    }
    ui->check_tor->setEnabled(!forced && !m_daemon->isRunning());
}

void AtomicSwapWidget::onDiscover() {
    if (m_daemon->isRunning()) {
        ui->btn_discover->setEnabled(false);
        m_daemon->stop();
        return;
    }

    const bool tor = torEnabledInSettings() || ui->check_tor->isChecked();
    if (!tor) {
        const auto answer = QMessageBox::warning(this, "Connect without Tor",
            "Without Tor, the rendezvous servers and every maker you connect to will see your IP address.\n\n"
            "Continue without Tor?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    m_summary = {};
    m_unavailableOffers = 0;
    m_failed = false;
    ui->tree_offers->clear();
    ui->label_details->clear();
    ui->label_peers->setText("Connected to 0 peers");
    ui->label_headline->setText(tor ? "Starting Tor..." : "Starting...");
    setBusy(true);
    ui->btn_discover->setText("Stop");
    ui->check_tor->setEnabled(false);

    m_daemon->start(tor);
}

void AtomicSwapWidget::onTorStatus(const QString &status) {
    if (status == "bootstrapping") {
        ui->label_headline->setText("Connecting to Tor...");
    } else if (status == "ready") {
        ui->label_headline->setText("Connected to Tor");
    }
}

void AtomicSwapWidget::onSummary(const atomic::DiscoverySummary &summary) {
    m_summary = summary;
    ui->label_headline->setText(atomic::discoveryHeadline(summary));
    ui->label_peers->setText(QString("Connected to %1 peers").arg(summary.connected));
    setBusy(atomic::discoveryActive(summary));
    updateDetails();
}

void AtomicSwapWidget::onOffers(const QList<atomic::MakerOffer> &offers) {
    QList<atomic::MakerOffer> available;
    for (const auto &offer : offers) {
        if (offer.available()) available.append(offer);
    }
    m_unavailableOffers = offers.size() - available.size();

    // Neutral order: the lowest price gives the most XMR for the BTC sent.
    std::sort(available.begin(), available.end(), [](const auto &a, const auto &b) {
        return a.priceSatPerXmr < b.priceSatPerXmr;
    });

    ui->tree_offers->clear();
    for (const auto &offer : available) {
        auto *item = new QTreeWidgetItem(ui->tree_offers);
        item->setText(Maker, offer.host());
        item->setText(Price, atomic::formatBtc(offer.priceSatPerXmr));
        item->setText(Min, atomic::formatBtc(offer.minSat));
        item->setText(Max, atomic::formatBtc(offer.maxSat));
        item->setText(Deposit, atomic::formatDeposit(offer.refundDeposit));
        for (int column : {Price, Min, Max, Deposit}) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }

        const QString tooltip = QString("Maker: %1\nAddress: %2\nSoftware version: %3")
            .arg(offer.peerId, offer.address, offer.version.isEmpty() ? "unknown" : offer.version);
        for (int column = Maker; column <= Deposit; column++) {
            item->setToolTip(column, tooltip);
        }
        item->setToolTip(Deposit, "Share of your BTC the maker may keep if the swap is refunded (anti-spam deposit).");
    }
    updateDetails();
}

void AtomicSwapWidget::onFailed(const QString &message) {
    m_failed = true;
    ui->label_headline->setText(QString("Error: %1").arg(message));
}

void AtomicSwapWidget::onFinished() {
    setBusy(false);
    ui->btn_discover->setText("Find makers");
    ui->btn_discover->setEnabled(!AtomicSwapDaemon::helperPath().isEmpty());
    if (!m_failed) {
        ui->label_headline->setText("Not searching");
    }
    updateTorOption();
}

void AtomicSwapWidget::setBusy(bool busy) {
    // Range 0..0 makes the bar animate, like eigenwallet's discovery bar.
    ui->progress->setMaximum(busy ? 0 : 1);
    ui->progress->setValue(0);
}

void AtomicSwapWidget::updateDetails() {
    QStringList parts;
    if (m_summary.makersKnown > 0) {
        parts << QString("%1 makers known").arg(m_summary.makersKnown);
    }
    const int available = ui->tree_offers->topLevelItemCount();
    if (available > 0 || m_summary.offers > 0) {
        parts << QString("%1 with XMR available").arg(available);
    }
    if (m_unavailableOffers > 0) {
        parts << QString("%1 connected without XMR right now").arg(m_unavailableOffers);
    }
    ui->label_details->setText(parts.join(" · "));
}
