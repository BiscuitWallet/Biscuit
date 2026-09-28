// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapWidget.h"
#include "ui_AtomicSwapWidget.h"

#include <algorithm>
#include <functional>

#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QTreeWidgetItem>

#include "AtomicSwapDialog.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "swap/core/Amount.h"
#include "utils/AppData.h"
#include "utils/Utils.h"
#include "widgets/PixelIcons.h"
#include "widgets/RetroBusyBar.h"
#include "utils/config.h"

using namespace biscuit::swap;

namespace {
    enum Column { Maker = 0, Price, Market, Min, Max, Deposit };

    // Market price from Biscuit's public price feed, 0 if unknown.
    double marketBtcPerXmr() {
        auto &prices = appData()->prices;
        return prices.canConvert("XMR", "BTC") ? prices.convert("XMR", "BTC", 1.0) : 0.0;
    }

    bool torEnabledInSettings() {
        return conf()->get(Config::proxy).toInt() != Config::Proxy::None;
    }
}

namespace {
    // Small drawn icons (16 px, sharp on Retina), in the text colour or green.
    QPixmap drawIcon(const std::function<void(QPainter &)> &draw) {
        const qreal dpr = 2.0;
        QPixmap pixmap(QSize(18, 18) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p);
        return pixmap;
    }

}

AtomicSwapWidget::AtomicSwapWidget(Wallet *wallet, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AtomicSwapWidget)
    , m_wallet(wallet)
    , m_daemon(new AtomicSwapDaemon(this))
    , m_runner(new AtomicSwapRunner(wallet, this))
{
    ui->setupUi(this);

    QHeaderView *swapsHeader = ui->tree_swaps->header();
    swapsHeader->setSectionResizeMode(QHeaderView::ResizeToContents);
    swapsHeader->setStretchLastSection(true);
    ui->label_swapActivity->setStyleSheet("color: gray;");
    connect(ui->btn_swap, &QPushButton::clicked, this, &AtomicSwapWidget::onSwap);
    connect(ui->tree_offers, &QTreeWidget::itemSelectionChanged, this, &AtomicSwapWidget::updateSwapButton);
    connect(ui->tree_offers, &QTreeWidget::itemDoubleClicked, this, &AtomicSwapWidget::onSwap);
    connect(m_runner, &AtomicSwapRunner::recordsChanged, this, &AtomicSwapWidget::refreshSwaps);
    connect(ui->btn_clearSwaps, &QPushButton::clicked, this, &AtomicSwapWidget::onClearSwaps);
    connect(m_runner, &AtomicSwapRunner::activity, this, [this](const QString &, const QString &text) {
        ui->label_swapActivity->setText(text);
    });
    refreshSwaps();

    QHeaderView *header = ui->tree_offers->header();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Maker, QHeaderView::Stretch);
    header->setStretchLastSection(false);

    ui->label_torNote->setStyleSheet("color: gray;");

    // Retro busy bar instead of the large modern one.
    m_busyBar = new RetroBusyBar(this);
    ui->layout_discovery->replaceWidget(ui->progress, m_busyBar);
    ui->progress->hide();

    m_phone = new QLabel(this);
    m_phone->setFixedSize(32, 32);
    m_phone->setAlignment(Qt::AlignCenter);
    m_phone->hide();
    ui->layout_headline->insertWidget(0, m_phone);
    m_dialTimer = new QTimer(this);
    m_dialTimer->setInterval(400);
    connect(m_dialTimer, &QTimer::timeout, this, [this] {
        m_dots = (m_dots + 1) % 4;
        ui->label_headline->setText(m_headlineBase + QString(m_dots, '.'));
        if (m_phoneState == Phone::Dialing) {
            // The dial's holes light up in turn while it dials.
            m_phone->setPixmap(PixelIcons::phone(false, ++m_modemPhase));
        }
    });
    ui->check_tor->setChecked(conf()->get(Config::atomicSwapTor).toBool());
    connect(ui->check_tor, &QCheckBox::toggled, this, [this](bool checked) {
        if (!torEnabledInSettings()) {
            conf()->set(Config::atomicSwapTor, checked);
        }
        updateTorOption();
    });

    connect(ui->btn_discover, &QPushButton::clicked, this, &AtomicSwapWidget::onDiscover);
    connect(m_daemon, &AtomicSwapDaemon::torStatusChanged, this, &AtomicSwapWidget::onTorStatus);
    connect(m_daemon, &AtomicSwapDaemon::discoveryStarted, this, [this] {
        setHeadline("Looking for makers", true);
    });
    connect(m_daemon, &AtomicSwapDaemon::summaryChanged, this, &AtomicSwapWidget::onSummary);
    connect(m_daemon, &AtomicSwapDaemon::offersChanged, this, &AtomicSwapWidget::onOffers);
    connect(m_daemon, &AtomicSwapDaemon::failed, this, &AtomicSwapWidget::onFailed);
    connect(m_daemon, &AtomicSwapDaemon::finished, this, &AtomicSwapWidget::onFinished);
    connect(&appData()->prices, &Prices::cryptoPricesUpdated, this, &AtomicSwapWidget::showOffers);

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

    // Opening the tab means the user wants offers: start searching once per
    // session, but only through Tor. Without Tor it waits for the click and
    // the IP warning.
    const bool tor = torEnabledInSettings() || ui->check_tor->isChecked();
    if (!m_autoStarted && tor && !m_daemon->isRunning() && !AtomicSwapDaemon::helperPath().isEmpty()) {
        m_autoStarted = true;
        onDiscover();
    }
}

void AtomicSwapWidget::updateTorOption() {
    // Tor mode: always Tor. Otherwise the user's choice, off by default.
    const bool forced = torEnabledInSettings();
    {
        const QSignalBlocker blocker(ui->check_tor);
        ui->check_tor->setChecked(forced || conf()->get(Config::atomicSwapTor).toBool());
    }
    ui->check_tor->setEnabled(!forced && !m_daemon->isRunning());

    if (forced) {
        ui->label_torNote->setText("Tor mode is on: makers are reached through Tor.");
    } else if (ui->check_tor->isChecked()) {
        ui->label_torNote->setText("Makers are reached through Tor. Finding them takes longer.");
    } else {
        ui->label_torNote->setText("Without Tor, the rendezvous servers and the makers you connect to see your IP address.");
    }
}

void AtomicSwapWidget::onDiscover() {
    if (m_daemon->isRunning()) {
        ui->btn_discover->setEnabled(false);
        m_daemon->stop();
        return;
    }

    const bool tor = torEnabledInSettings() || ui->check_tor->isChecked();

    m_summary = {};
    m_offers.clear();
    m_unavailableOffers = 0;
    m_failed = false;
    ui->tree_offers->clear();
    ui->label_details->clear();
    ui->label_peers->setText("Connected to 0 peers");
    setHeadline(tor ? "Starting Tor" : "Starting", true);
    setPhone(Phone::Waiting);
    setBusy(true);
    ui->btn_discover->setText("Stop");
    ui->check_tor->setEnabled(false);

    m_daemon->start(tor);
}

void AtomicSwapWidget::onTorStatus(const QString &status) {
    if (status == "bootstrapping") {
        setHeadline("Connecting to Tor", true);
    } else if (status == "ready") {
        setHeadline("Connected to Tor, dialing makers", true);
    }
}

void AtomicSwapWidget::onSummary(const atomic::DiscoverySummary &summary) {
    m_summary = summary;
    const bool active = atomic::discoveryActive(summary);
    QString headline = atomic::discoveryHeadline(summary);
    if (headline == QLatin1String("Dialing peers...")) {
        headline = "Dialing makers...";
    }
    setHeadline(headline, active);
    // Modem dialing, then connected while offers come in; hourglass when
    // waiting. The check mark only when the search is over (onFinished).
    if (summary.dialing > 0 && summary.connected == 0) {
        setPhone(Phone::Dialing);
    } else if (active) {
        setPhone(Phone::PickedUp);
    } else {
        setPhone(Phone::Waiting);
    }
    ui->label_peers->setText(QString("Connected to %1 peers").arg(summary.connected));
    setBusy(atomic::discoveryActive(summary));
    updateDetails();
}

void AtomicSwapWidget::onOffers(const QList<atomic::MakerOffer> &offers) {
    m_offers = offers;
    showOffers();
}

void AtomicSwapWidget::showOffers() {
    QList<atomic::MakerOffer> available;
    for (const auto &offer : m_offers) {
        if (offer.available()) available.append(offer);
    }
    m_unavailableOffers = m_offers.size() - available.size();
    const double market = marketBtcPerXmr();

    // Neutral order: the lowest price gives the most XMR for the BTC sent.
    std::sort(available.begin(), available.end(), [](const auto &a, const auto &b) {
        return a.priceSatPerXmr < b.priceSatPerXmr;
    });

    ui->tree_offers->clear();
    m_shownOffers = available;
    for (int row = 0; row < available.size(); ++row) {
        const auto &offer = available.at(row);
        auto *item = new QTreeWidgetItem(ui->tree_offers);
        item->setData(Maker, Qt::UserRole, row);
        item->setText(Maker, offer.host());
        item->setText(Price, atomic::formatBtc(offer.priceSatPerXmr));

        if (const auto deviation = atomic::marketDeviation(offer.priceSatPerXmr, market)) {
            item->setText(Market, atomic::formatDeviation(*deviation));
            if (atomic::deviationNeedsWarning(*deviation)) {
                item->setForeground(Market, QBrush(Qt::red));
            }
        } else {
            item->setText(Market, "—");
        }
        item->setText(Min, atomic::formatBtc(offer.minSat));
        item->setText(Max, atomic::formatBtc(offer.maxSat));
        item->setText(Deposit, atomic::formatDeposit(offer.refundDeposit));
        for (int column : {Price, Market, Min, Max, Deposit}) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }

        const QString tooltip = QString("Maker: %1\nAddress: %2\nSoftware version: %3")
            .arg(offer.peerId, offer.address, offer.version.isEmpty() ? "unknown" : offer.version);
        for (int column = Maker; column <= Deposit; column++) {
            item->setToolTip(column, tooltip);
        }
        item->setToolTip(Deposit, "Share of your BTC the maker may keep if the swap is refunded (anti-spam deposit).");
        item->setToolTip(Market, market > 0
            ? QString("Compared with the market price of %1 BTC per XMR (Biscuit's public price data). "
                      "Above +5%: expensive. Far below the market: the maker's price may be out of date.")
                  .arg(QString::number(market, 'f', 8))
            : QString("No market price: public price data is off or not loaded yet."));
    }
    updateDetails();
    updateSwapButton();
}

void AtomicSwapWidget::onFailed(const QString &message) {
    m_failed = true;
    setHeadline(QString("Error: %1").arg(message), false);
    setPhone(Phone::Hidden);
}

void AtomicSwapWidget::onFinished() {
    setBusy(false);
    ui->btn_discover->setText("Find makers");
    ui->btn_discover->setEnabled(!AtomicSwapDaemon::helperPath().isEmpty());
    if (!m_failed) {
        setHeadline("Not searching", false);
    }
    setPhone(m_summary.offers > 0 && !m_failed ? Phone::Done : Phone::Hidden);
    updateTorOption();
}

void AtomicSwapWidget::setHeadline(const QString &text, bool dialing) {
    QString base = text;
    while (base.endsWith('.')) {
        base.chop(1);
    }
    m_headlineBase = base;
    if (dialing) {
        if (!m_dialTimer->isActive()) {
            m_dots = 0;
            m_dialTimer->start();
        }
        ui->label_headline->setText(m_headlineBase + QString(m_dots, '.'));
    } else {
        m_dialTimer->stop();
        ui->label_headline->setText(text);
    }
}

void AtomicSwapWidget::setPhone(Phone phone) {
    if (phone == m_phoneState) {
        return;
    }
    m_phoneState = phone;
    const QColor green(46, 160, 67);
    switch (phone) {
    case Phone::Hidden:
        m_phone->hide();
        return;
    case Phone::Waiting:
        m_phone->setPixmap(PixelIcons::hourglass());
        m_phone->setToolTip("Waiting");
        break;
    case Phone::Dialing:
        m_phone->setPixmap(PixelIcons::phone(false, m_modemPhase));
        m_phone->setToolTip("Dialing makers");
        break;
    case Phone::PickedUp:
        m_phone->setPixmap(PixelIcons::phone(true, 0));
        m_phone->setToolTip("A maker answered");
        break;
    case Phone::Done:
        m_phone->setPixmap(drawIcon([green](QPainter &p) {
            p.setPen(QPen(green, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath check;
            check.moveTo(3.5, 9.5);
            check.lineTo(7.5, 13.5);
            check.lineTo(14.5, 5.0);
            p.drawPath(check);
        }));
        m_phone->setToolTip("Offers received");
        break;
    }
    m_phone->show();
}

void AtomicSwapWidget::setBusy(bool busy) {
    m_busyBar->setBusy(busy);
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

void AtomicSwapWidget::updateSwapButton() {
    const auto items = ui->tree_offers->selectedItems();
    ui->btn_swap->setEnabled(!items.isEmpty() && !m_runner->busy() && !AtomicSwapDaemon::helperPath().isEmpty());
    ui->btn_swap->setToolTip(m_runner->busy() ? "Another atomic swap is still in progress." : QString());
}

void AtomicSwapWidget::onSwap() {
    const auto items = ui->tree_offers->selectedItems();
    if (items.isEmpty() || m_runner->busy()) {
        return;
    }
    const int row = items.first()->data(Maker, Qt::UserRole).toInt();
    if (row < 0 || row >= m_shownOffers.size()) {
        return;
    }
    const atomic::MakerOffer offer = m_shownOffers.at(row);

    using namespace biscuit::coins;
    CoinVault *vault = CoinVault::forWallet(m_wallet);
    CoinWallet *btc = vault && vault->isUnlocked() ? vault->bitcoin() : nullptr;
    if (!btc) {
        Utils::showError(this, "Bitcoin wallet not open",
                         "Open or set up your Bitcoin wallet first (Receive → Bitcoin): the swap pays from it.");
        return;
    }
    if (btc->status() != CoinWallet::Status::Synchronized) {
        Utils::showError(this, "Bitcoin wallet not ready",
                         "Your Bitcoin wallet is still synchronizing. Try again in a moment.");
        return;
    }

    const bool tor = torEnabledInSettings() || ui->check_tor->isChecked();
    AtomicSwapDialog dialog(offer, vault->selectedName(bitcoin()), btc->balance().confirmed, tor,
                            marketBtcPerXmr(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    if (!m_runner->start(offer, dialog.btcSat(), tor, &error)) {
        Utils::showError(this, "Could not start the swap", error);
    }
    updateSwapButton();
}

void AtomicSwapWidget::refreshSwaps() {
    const auto records = m_runner->records();
    ui->tree_swaps->clear();
    for (const auto &r : records) {
        auto *item = new QTreeWidgetItem(ui->tree_swaps);
        item->setText(0, QLocale().toString(r.created.toLocalTime(), QLocale::ShortFormat));
        item->setText(1, r.makerHost);
        item->setText(2, atomic::formatBtc(r.btcSat));
        const quint64 xmr = r.expectedXmrAtomic();
        item->setText(3, xmr ? QString("≈ %1").arg(amount::fromAtomic(xmr, 12)) : QString("—"));
        QString status = atomic::stageText(r.stage);
        if (r.stage == atomic::stage::cancelled && !r.error.isEmpty()) {
            // Why it stopped, e.g. the maker's real minimum.
            status = QString("Cancelled, no BTC sent · %1").arg(atomic::failureReason(r.error));
        } else if (!r.error.isEmpty() && !atomic::isFinalStage(r.stage)) {
            status += " · retrying";
        }
        item->setText(4, status);
        for (int column : {2, 3}) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
        QStringList tooltip = {QString("Swap ID: %1").arg(r.id), QString("Maker: %1").arg(r.makerPeerId),
                               QString("XMR to: %1").arg(r.xmrAddress)};
        if (r.lockFeeSat) tooltip << QString("Bitcoin network fee: %1 BTC").arg(atomic::formatBtc(r.lockFeeSat));
        if (!r.stateText.isEmpty()) tooltip << QString("Step: %1").arg(r.stateText);
        if (!r.error.isEmpty()) tooltip << QString("Last error: %1").arg(r.error);
        for (int column = 0; column < 5; ++column) {
            item->setToolTip(column, tooltip.join("\n"));
        }
    }
    ui->label_swapsTitle->setVisible(!records.isEmpty());
    ui->tree_swaps->setVisible(!records.isEmpty());
    // Only finished swaps can be cleared: nothing to clear, no button.
    ui->btn_clearSwaps->setVisible(atomic::withoutFinished(records, m_runner->runningSwapId()).size() < records.size());
    updateSwapButton();
}

// "Clear history": finished swaps leave the list; swaps in progress stay.
void AtomicSwapWidget::onClearSwaps() {
    const auto answer = QMessageBox::question(this, "Clear history",
        "Remove finished atomic swaps from this list?\n\n"
        "Swaps still in progress stay, and their logs are kept.");
    if (answer == QMessageBox::Yes) {
        m_runner->clearFinished();
    }
}
