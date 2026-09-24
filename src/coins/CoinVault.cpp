// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinVault.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>

#include "Bip39.h"
#include "libwalletqt/Wallet.h"
#include "utils/NetworkManager.h"
#include "utils/config.h"

namespace biscuit::coins {

namespace {
    constexpr int fileVersion = 1;
    constexpr int saveDelayMs = 3000;

    QHash<Wallet *, QPointer<CoinVault>> &vaults() {
        static QHash<Wallet *, QPointer<CoinVault>> map;
        return map;
    }

    QJsonObject parseContent(const QByteArray &content) {
        return QJsonDocument::fromJson(content).object();
    }
}

CoinVault *CoinVault::forWallet(Wallet *wallet) {
    auto &map = vaults();
    if (auto existing = map.value(wallet)) {
        return existing;
    }
    auto *vault = new CoinVault(wallet);
    map.insert(wallet, vault);
    return vault;
}

CoinVault::CoinVault(Wallet *wallet)
    : QObject(wallet)
    , m_wallet(wallet)
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &CoinVault::save);
    connect(this, &QObject::destroyed, [wallet] { vaults().remove(wallet); });
}

QString CoinVault::path() const {
    QString keys = m_wallet ? m_wallet->keysPath() : QString();
    if (keys.endsWith(".keys")) {
        keys.chop(5);
    }
    return keys + ".btcltc";
}

bool CoinVault::exists() const {
    return QFile::exists(path());
}

bool CoinVault::setUp(const QString &mnemonic, const QString &passphrase, const QString &password, QString *error) {
    if (!m_wallet || !m_wallet->verifyPassword(password)) {
        if (error) *error = "Wrong password (use the password of this Monero wallet)";
        return false;
    }
    if (!bip39::isValidMnemonic(mnemonic)) {
        if (error) *error = "Invalid seed phrase";
        return false;
    }
    if (exists()) {
        if (error) *error = "A Bitcoin/Litecoin wallet already exists for this wallet";
        return false;
    }

    auto session = walletfile::Session::create(password);
    if (!session) {
        if (error) *error = "Unable to encrypt the wallet file";
        return false;
    }
    m_created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QByteArray content = QJsonDocument(QJsonObject{
        {"version", fileVersion},
        {"mnemonic", bip39::normalizeMnemonic(mnemonic)},
        {"passphrase", passphrase},
        {"created", m_created},
        {"coins", QJsonObject{}},
    }).toJson(QJsonDocument::Compact);

    const bool saved = session->save(path(), content, error);
    const bool loaded = saved && load(content, error);
    walletfile::wipe(content);
    if (!loaded) {
        return false;
    }
    m_session = std::move(*session);
    emit unlocked();
    return true;
}

bool CoinVault::unlock(const QString &password, QString *error) {
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    auto opened = walletfile::Session::open(file.readAll(), password);
    if (!opened) {
        if (error) *error = "Wrong password or damaged file";
        return false;
    }
    const bool loaded = load(opened->second, error);
    walletfile::wipe(opened->second);
    if (!loaded) {
        return false;
    }
    m_session = std::move(opened->first);
    emit unlocked();
    return true;
}

bool CoinVault::load(const QByteArray &content, QString *error) {
    const QJsonObject obj = parseContent(content);
    if (obj.value("version").toInt() != fileVersion) {
        if (error) *error = "Unsupported wallet file version";
        return false;
    }
    const QString mnemonic = obj.value("mnemonic").toString();
    const auto seed = bip39::mnemonicToSeed(mnemonic, obj.value("passphrase").toString());
    if (!seed) {
        if (error) *error = "Invalid seed in wallet file";
        return false;
    }
    QByteArray seedBytes = *seed;
    auto btc = HdAccount::fromSeed(seedBytes, coins::bitcoin());
    auto ltc = HdAccount::fromSeed(seedBytes, coins::litecoin());
    walletfile::wipe(seedBytes);
    if (!btc || !ltc) {
        if (error) *error = "Unable to derive keys";
        return false;
    }

    m_created = obj.value("created").toString();
    const QJsonObject caches = obj.value("coins").toObject();
    m_btc = new CoinWallet(std::move(*btc), caches.value("BTC").toObject(), this);
    m_ltc = new CoinWallet(std::move(*ltc), caches.value("LTC").toObject(), this);
    for (CoinWallet *w : {m_btc, m_ltc}) {
        connect(w, &CoinWallet::cacheChanged, this, &CoinVault::scheduleSave);
    }
    applyNetworkSettings();   // sets the proxy and starts the wallets
    return true;
}

void CoinVault::lock() {
    if (!m_session) {
        return;
    }
    m_saveTimer.stop();
    save();
    delete m_btc;
    delete m_ltc;
    m_btc = m_ltc = nullptr;
    m_session.reset();   // wipes the key
    emit locked();
}

std::optional<QPair<QString, QString>> CoinVault::revealMnemonic(const QString &password, QString *error) const {
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    auto content = walletfile::decrypt(file.readAll(), password);
    if (!content) {
        if (error) *error = "Wrong password";
        return std::nullopt;
    }
    const QJsonObject obj = parseContent(*content);
    walletfile::wipe(*content);
    return qMakePair(obj.value("mnemonic").toString(), obj.value("passphrase").toString());
}

CoinWallet *CoinVault::wallet(const CoinParams &params) const {
    if (params == coins::bitcoin()) return m_btc;
    if (params == coins::litecoin()) return m_ltc;
    return nullptr;
}

void CoinVault::applyNetworkSettings() {
    const int proxy = conf()->get(Config::proxy).toInt();
    QNetworkProxy networkProxy(QNetworkProxy::NoProxy);
    if (proxy != Config::Proxy::None) {
        // Same SOCKS5 proxy as the rest of the app (Tor managed by Biscuit or not).
        networkProxy = getNetworkSocks5()->proxy();
    }
    const bool onionOnly = proxy == Config::Proxy::Tor && conf()->get(Config::torOnlyAllowOnion).toBool();
    for (CoinWallet *w : {m_btc, m_ltc}) {
        if (!w) continue;
        // No onion Electrum server is built in yet: in onion-only mode the
        // wallets stay offline rather than contacting clearnet servers.
        if (onionOnly) {
            w->stop();
            continue;
        }
        w->setProxy(networkProxy);   // reconnects if running
        if (!w->isRunning()) w->start();
    }
}

void CoinVault::scheduleSave() {
    m_saveTimer.start(saveDelayMs);
}

void CoinVault::save() {
    if (!m_session || !m_btc || !m_ltc) {
        return;
    }
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    // Read the seed back with the session key (it is not kept in memory),
    // replace the caches, write again with a fresh nonce.
    auto content = m_session->unseal(file.readAll());
    file.close();
    if (!content) {
        qWarning() << "Biscuit: unable to update the Bitcoin/Litecoin wallet file";
        return;
    }
    QJsonObject obj = parseContent(*content);
    walletfile::wipe(*content);
    obj["coins"] = QJsonObject{{"BTC", m_btc->cache()}, {"LTC", m_ltc->cache()}};
    QByteArray updated = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    obj = {};
    QString error;
    if (!m_session->save(path(), updated, &error)) {
        qWarning() << "Biscuit: unable to save the Bitcoin/Litecoin wallet file:" << error;
    }
    walletfile::wipe(updated);
}

}
