// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapRunner.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QUuid>

#include "AtomicSwapDaemon.h"
#include "coins/CoinVault.h"
#include "coins/CoinWallet.h"
#include "coins/core/WalletFile.h"
#include "libwalletqt/Subaddress.h"
#include "libwalletqt/rows/SubaddressRow.h"
#include "libwalletqt/Wallet.h"
#include "utils/NetworkManager.h"
#include "utils/config.h"

namespace biscuit::swap {

using namespace atomic;

namespace {
    // After a failure with BTC locked, try again: the swap must go on (or be
    // refunded) and most errors are network ones.
    constexpr int retryDelayMs = 60 * 1000;

    QString swapsDir() {
        return Config::defaultConfigDir().filePath("atomicswaps");
    }

    QString logPath(const QString &id) {
        return QDir(swapsDir()).filePath(QString("logs/%1.log").arg(id));
    }

    // The maker's last answer during setup, from the swap's log: the helper's
    // final error may only say that setup timed out.
    QString lastSetupRefusal(const QString &id) {
        QFile file(logPath(id));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        const qint64 tail = 64 * 1024;
        if (file.size() > tail) {
            file.seek(file.size() - tail);
        }
        static const QRegularExpression ansi("\x1b\\[[0-9;]*m");
        static const QRegularExpression refusal("Swap setup failed error=Protocol\\(\"([^\"]+)");
        QString last;
        for (const QString &line : QString::fromUtf8(file.readAll()).remove(ansi).split('\n')) {
            if (const auto m = refusal.match(line); m.hasMatch()) {
                last = m.captured(1);
            }
        }
        return last;
    }

    bool biscuitUsesProxy() {
        return conf()->get(Config::proxy).toInt() != Config::Proxy::None;
    }
}

AtomicSwapRunner::AtomicSwapRunner(Wallet *wallet, QObject *parent)
    : QObject(parent)
    , m_wallet(wallet)
{
    m_records = recordsFromJson(m_wallet->getCacheAttribute(walletAttribute).toUtf8());
    // Swaps that stopped before this was recorded: add the maker's reason.
    for (AtomicSwapRecord &r : m_records) {
        const QString refusal = r.stage == stage::cancelled ? lastSetupRefusal(r.id) : QString();
        if (!refusal.isEmpty() && !r.error.contains(refusal)) {
            r.error = refusal + "\n" + r.error;
        }
    }

    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(retryDelayMs);
    connect(&m_retryTimer, &QTimer::timeout, this, &AtomicSwapRunner::resumeNext);

    // Unfinished swaps go on as soon as the Bitcoin wallet is available.
    if (auto *vault = coins::CoinVault::forWallet(m_wallet)) {
        connect(vault, &coins::CoinVault::unlocked, this, &AtomicSwapRunner::resumeNext);
        if (vault->isUnlocked()) {
            QTimer::singleShot(0, this, &AtomicSwapRunner::resumeNext);
        }
    }
}

AtomicSwapRunner::~AtomicSwapRunner() {
    // The swap is saved at every step: it resumes at the next start.
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

bool AtomicSwapRunner::busy() const {
    if (m_process) {
        return true;
    }
    return std::any_of(m_records.begin(), m_records.end(), [](const AtomicSwapRecord &r) {
        return fundsAtStake(r.stage);
    });
}

bool AtomicSwapRunner::start(const MakerOffer &offer, quint64 btcSat, bool tor, QString *error) {
    if (busy()) {
        if (error) *error = "Another atomic swap is still in progress.";
        return false;
    }
    auto *vault = coins::CoinVault::forWallet(m_wallet);
    if (!vault || !vault->isUnlocked() || !vault->bitcoin()) {
        if (error) *error = "Your Bitcoin wallet is not open.";
        return false;
    }

    AtomicSwapRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.walletId = vault->selectedId(coins::bitcoin());
    record.makerPeerId = offer.peerId;
    record.makerAddress = offer.address;
    record.makerHost = offer.host();
    record.priceSatPerXmr = offer.priceSatPerXmr;
    record.btcSat = btcSat;
    record.tor = tor;
    record.created = QDateTime::currentDateTimeUtc();
    record.xmrAddress = swapSubaddress(QString("Atomic swap BTC → XMR · %1").arg(record.makerHost));
    if (record.xmrAddress.isEmpty()) {
        if (error) *error = "Could not create a Monero subaddress for the swap.";
        return false;
    }

    m_records.prepend(record);
    save();
    if (!launch(record, false, error)) {
        update(record.id, [error](AtomicSwapRecord &r) {
            r.stage = stage::cancelled;
            r.error = error ? *error : QString();
        });
        return false;
    }
    return true;
}

void AtomicSwapRunner::resumeNext() {
    if (m_process) {
        return;
    }
    auto *vault = coins::CoinVault::forWallet(m_wallet);
    if (!vault || !vault->isUnlocked()) {
        return;
    }
    for (const AtomicSwapRecord &record : m_records) {
        // "setup": nothing was locked, the maker has forgotten the offer.
        if (!fundsAtStake(record.stage)) {
            continue;
        }
        QString error;
        if (!launch(record, true, &error)) {
            update(record.id, [&error](AtomicSwapRecord &r) { r.error = error; });
            m_retryTimer.start();
        }
        return;
    }
}

bool AtomicSwapRunner::launch(const AtomicSwapRecord &record, bool resume, QString *error) {
    const QString program = AtomicSwapDaemon::helperPath();
    if (program.isEmpty()) {
        if (error) *error = "The swap helper (biscuit-swapd) was not found.";
        return false;
    }
    auto *vault = coins::CoinVault::forWallet(m_wallet);
    const auto list = vault ? vault->wallets(coins::bitcoin()) : QList<coins::CoinVault::Entry>{};
    const auto entry = std::find_if(list.begin(), list.end(), [&record](const auto &e) { return e.id == record.walletId; });
    if (entry == list.end()) {
        if (error) *error = "The Bitcoin wallet of this swap is not in Biscuit any more.";
        return false;
    }
    coins::CoinWallet *btc = entry->wallet;

    // The order, without the seed first. The helper checks that the key it
    // derives gives this wallet's first address before doing anything.
    QJsonObject request{{"expected_first_address", btc->firstAddress()}};
    if (!resume) {
        const QString change = btc->receiveAddress();
        if (change.isEmpty()) {
            if (error) *error = "Your Bitcoin wallet is not ready yet. Try again in a moment.";
            return false;
        }
        request = {
            {"expected_first_address", btc->firstAddress()},
            {"swap_id", record.id},
            {"maker_peer_id", record.makerPeerId},
            {"maker_address", record.makerAddress},
            {"btc_amount_sat", static_cast<qint64>(record.btcSat)},
            {"xmr_address", record.xmrAddress},
            {"change_address", change},
        };
    }
    // ...then the seed spliced in, in buffers that are wiped afterwards.
    auto seed = vault->bip39Seed(record.walletId, error);
    if (!seed) {
        return false;
    }
    QByteArray seedHex = seed->toHex();
    coins::walletfile::wipe(*seed);
    const QByteArray rest = QJsonDocument(request).toJson(QJsonDocument::Compact);   // "{...}"
    QByteArray input = "{\"seed_hex\":\"" + seedHex + "\"," + rest.mid(1);
    coins::walletfile::wipe(seedHex);

    const QString dataDir = swapsDir();
    QDir().mkpath(QDir(dataDir).filePath("logs"));

    QStringList args;
    if (resume) {
        args << "resume" << record.id;
    } else {
        args << "buy";
    }
    args << (record.tor ? "--tor" : "--clearnet") << "--data-dir" << dataDir;
    for (const QString &url : btc->electrumUrls()) {
        args << "--electrum" << url;
    }
    // Electrum through the same proxy as Biscuit's own Bitcoin wallet.
    if (biscuitUsesProxy()) {
        const QNetworkProxy proxy = getNetworkSocks5()->proxy();
        args << "--electrum-socks5" << QString("%1:%2").arg(proxy.hostName()).arg(proxy.port());
    }

    m_lastError.clear();
    m_runningId = record.id;
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RUST_LOG", "warn,swap=info,biscuit_swapd=info");
    m_process->setProcessEnvironment(env);
    // A log per swap, kept for troubleshooting (no secret in it).
    m_process->setStandardErrorFile(logPath(record.id), QIODevice::Append);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &AtomicSwapRunner::onReadyRead);
    connect(m_process, &QProcess::finished, this, &AtomicSwapRunner::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart) {
            m_lastError = "The swap helper could not be started.";
            onFinished(-1, QProcess::CrashExit);
        }
    });

    m_process->start();
    m_process->write(input);
    coins::walletfile::wipe(input);
    m_process->closeWriteChannel();

    update(record.id, [](AtomicSwapRecord &r) { r.error.clear(); });
    emit activity(record.id, record.tor ? "Connecting to Tor" : "Starting");
    return true;
}

void AtomicSwapRunner::onReadyRead() {
    while (m_process && m_process->canReadLine()) {
        const Event event = parseLine(m_process->readLine().trimmed());
        const QString id = m_runningId;

        switch (event.type) {
            case Event::Type::Tor:
                emit activity(id, event.torStatus == "ready" ? "Connected to Tor" : "Connecting to Tor");
                break;
            case Event::Type::Bitcoin:
                emit activity(id, event.bitcoinStatus == "ready" ? "Contacting the maker" : "Syncing the Bitcoin wallet");
                break;
            case Event::Type::SwapStarted:
                update(id, [&event](AtomicSwapRecord &r) { r.lockFeeSat = event.lockFeeSat; });
                break;
            case Event::Type::SwapResumed:
                emit activity(id, "Resuming the swap");
                break;
            case Event::Type::SwapState:
            case Event::Type::SwapFinished:
                if (event.swapId == id) {
                    update(id, [&event](AtomicSwapRecord &r) {
                        r.stage = event.stage;
                        r.stateText = event.stateText;
                    });
                    emit activity(id, stageText(event.stage));
                }
                break;
            case Event::Type::Error:
                m_lastError = event.message;
                break;
            case Event::Type::Started:
            case Event::Type::Summary:
            case Event::Type::Offers:
            case Event::Type::Stopped:
            case Event::Type::Ignored:
            case Event::Type::Invalid:
                break;
        }
    }
}

void AtomicSwapRunner::onFinished(int exitCode, QProcess::ExitStatus status) {
    onReadyRead();
    const QString id = m_runningId;
    const bool failed = status == QProcess::CrashExit || exitCode != 0;
    const QString error = !m_lastError.isEmpty() ? m_lastError
                        : failed ? QString("The swap helper stopped unexpectedly.") : QString();

    if (m_process) {
        m_process->deleteLater();
    }
    m_process.clear();
    m_runningId.clear();

    update(id, [&error, &id](AtomicSwapRecord &r) {
        if (!error.isEmpty()) {
            r.error = error;
        }
        // Stopped before the BTC was locked: nothing happened, it is over.
        if (r.stage == stage::setup) {
            r.stage = stage::cancelled;
            // Keep the maker's own reason when it refused.
            const QString refusal = lastSetupRefusal(id);
            if (!refusal.isEmpty() && !r.error.contains(refusal)) {
                r.error = refusal + "\n" + r.error;
            }
        }
    });

    const auto record = std::find_if(m_records.begin(), m_records.end(), [&id](const auto &r) { return r.id == id; });
    if (record != m_records.end() && fundsAtStake(record->stage)) {
        emit activity(id, QString("Interrupted, retrying in a minute: %1").arg(error));
        m_retryTimer.start();
    } else {
        emit activity(id, QString());
        resumeNext();
    }
}

void AtomicSwapRunner::update(const QString &id, const std::function<void(AtomicSwapRecord &)> &change) {
    for (AtomicSwapRecord &record : m_records) {
        if (record.id == id) {
            change(record);
            save();
            return;
        }
    }
}

int AtomicSwapRunner::clearFinished() {
    const auto kept = withoutFinished(m_records, m_runningId);
    const int removed = int(m_records.size() - kept.size());
    if (removed > 0) {
        m_records = kept;
        save();
    }
    return removed;
}

void AtomicSwapRunner::save() {
    if (m_wallet) {
        m_wallet->setCacheAttribute(walletAttribute, QString::fromUtf8(recordsToJson(m_records)));
        // Written at once: the stage decides whether the swap resumes.
        m_wallet->storeSafer();
    }
    emit recordsChanged();
}

QString AtomicSwapRunner::swapSubaddress(const QString &label) {
    if (!m_wallet) {
        return {};
    }
    // A swap cancelled during setup never used its subaddress (no XMR could
    // be sent to it): give it to the new swap instead of adding another one.
    auto usedElsewhere = [this](const QString &address) {
        return std::any_of(m_records.begin(), m_records.end(), [&address](const AtomicSwapRecord &r) {
            return r.xmrAddress == address && r.stage != stage::cancelled;
        });
    };
    const quint32 account = m_wallet->currentSubaddressAccount();
    for (const AtomicSwapRecord &r : m_records) {
        if (r.stage != stage::cancelled || r.xmrAddress.isEmpty() || usedElsewhere(r.xmrAddress)) {
            continue;
        }
        const SubaddressIndex index = m_wallet->subaddressIndex(r.xmrAddress);
        if (!index.isValid() || static_cast<quint32>(index.major) != account) {
            continue;
        }
        const auto rows = m_wallet->subaddress()->getRows();
        const auto row = std::find_if(rows.begin(), rows.end(), [&r](const SubaddressRow &s) { return s.address == r.xmrAddress; });
        if (row == rows.end() || row->used) {
            continue;
        }
        m_wallet->subaddress()->setLabel(index.minor, label);
        return r.xmrAddress;
    }

    if (!m_wallet->subaddress()->addRow(label)) {
        return {};
    }
    const quint32 count = m_wallet->numSubaddresses(account);
    return count > 0 ? m_wallet->address(account, count - 1) : QString();
}

}
