// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinVault.h"

#include <algorithm>

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUuid>

#include "Bip39.h"
#include "libwalletqt/Wallet.h"
#include "utils/NetworkManager.h"
#include "utils/config.h"

namespace biscuit::coins {

namespace {
    // 1: main seed only. 2: adds "extra" wallets and "selected".
    constexpr int fileVersion = 2;
    constexpr char mainName[] = "Main";

    QString mainId(const CoinParams &params) {
        return QString("main-%1").arg(params.ticker);
    }

    const CoinParams *paramsForTicker(const QString &ticker) {
        if (ticker == coins::bitcoin().ticker) return &coins::bitcoin();
        if (ticker == coins::litecoin().ticker) return &coins::litecoin();
        return nullptr;
    }
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
        {"extra", QJsonArray{}},
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
    const int version = obj.value("version").toInt();
    if (version < 1 || version > fileVersion) {
        if (error) *error = "Unsupported wallet file version";
        return false;
    }
    const auto seed = bip39::mnemonicToSeed(obj.value("mnemonic").toString(), obj.value("passphrase").toString());
    if (!seed) {
        if (error) *error = "Invalid seed in wallet file";
        return false;
    }
    m_created = obj.value("created").toString();
    const QJsonObject caches = obj.value("coins").toObject();
    QByteArray seedBytes = *seed;
    bool ok = true;
    for (const CoinParams *params : {&coins::bitcoin(), &coins::litecoin()}) {
        ok = ok && addEntry(mainId(*params), mainName, true, *params, seedBytes,
                            caches.value(params->ticker).toObject(), error);
    }
    walletfile::wipe(seedBytes);

    if (!ok) {
        clearWallets();
        return false;
    }

    // An unreadable added wallet is skipped, it never blocks the others.
    for (const QJsonValue &value : obj.value("extra").toArray()) {
        const QJsonObject extra = value.toObject();
        const CoinParams *params = paramsForTicker(extra.value("coin").toString());
        const auto extraSeed = bip39::mnemonicToSeed(extra.value("mnemonic").toString(), extra.value("passphrase").toString());
        if (!params || !extraSeed) {
            qWarning() << "Biscuit: skipping an unreadable Bitcoin/Litecoin wallet entry";
            continue;
        }
        QByteArray extraBytes = *extraSeed;
        QString extraError;
        if (!addEntry(extra.value("id").toString(), extra.value("name").toString(), false, *params, extraBytes,
                      extra.value("cache").toObject(), &extraError)) {
            qWarning() << "Biscuit: skipping a Bitcoin/Litecoin wallet:" << extraError;
        }
        walletfile::wipe(extraBytes);
    }

    const QJsonObject selected = obj.value("selected").toObject();
    for (const CoinParams *params : {&coins::bitcoin(), &coins::litecoin()}) {
        const QString id = selected.value(params->ticker).toString();
        const auto list = wallets(*params);
        const bool known = std::any_of(list.begin(), list.end(), [&id](const Entry &e) { return e.id == id; });
        m_selected[params->ticker] = known ? id : mainId(*params);
    }
    applyNetworkSettings();   // sets the proxy and starts the wallets
    return true;
}

bool CoinVault::addEntry(const QString &id, const QString &name, bool mainSeed, const CoinParams &params,
                         const QByteArray &seed, const QJsonObject &cache, QString *error) {
    auto account = HdAccount::fromSeed(seed, params);
    if (!account) {
        if (error) *error = "Unable to derive keys";
        return false;
    }
    auto *w = new CoinWallet(std::move(*account), cache, this);
    connect(w, &CoinWallet::cacheChanged, this, &CoinVault::scheduleSave);
    connect(w, &CoinWallet::updated, this, &CoinVault::walletUpdated);
    connect(w, &CoinWallet::statusChanged, this, &CoinVault::walletUpdated);
    m_entries.append({id, name, mainSeed, w});
    return true;
}

void CoinVault::clearWallets() {
    for (const Entry &e : m_entries) {
        delete e.wallet;
    }
    m_entries.clear();
    m_selected.clear();
}

void CoinVault::lock() {
    if (!m_session) {
        return;
    }
    m_saveTimer.stop();
    save();
    clearWallets();
    m_session.reset();   // wipes the key
    emit locked();
}

bool CoinVault::checkPassword(const QString &password) const {
    if (!exists()) {
        return true;
    }
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    auto content = walletfile::decrypt(file.readAll(), password);
    if (content) walletfile::wipe(*content);
    return content.has_value();
}

bool CoinVault::changePassword(const QString &oldPassword, const QString &newPassword, QString *error) {
    if (!exists()) {
        return true;
    }
    m_saveTimer.stop();
    save();   // latest caches first

    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    auto opened = walletfile::Session::open(file.readAll(), oldPassword);
    file.close();
    if (!opened) {
        if (error) *error = "Wrong password";
        return false;
    }
    auto session = walletfile::Session::create(newPassword);
    if (!session) {
        walletfile::wipe(opened->second);
        if (error) *error = "Unable to encrypt the wallet file";
        return false;
    }
    const bool saved = session->save(path(), opened->second, error);
    walletfile::wipe(opened->second);
    if (saved && m_session) {
        m_session = std::move(*session);   // keep saving with the new key
    }
    return saved;
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
    const QString id = selectedId(params);
    for (const Entry &e : m_entries) {
        if (e.id == id) return e.wallet;
    }
    return nullptr;
}

QString CoinVault::selectedId(const CoinParams &params) const {
    return m_selected.value(params.ticker, mainId(params));
}

QString CoinVault::selectedName(const CoinParams &params) const {
    const QString id = selectedId(params);
    for (const Entry &e : m_entries) {
        if (e.id == id) return e.name;
    }
    return {};
}

void CoinVault::select(const CoinParams &params, const QString &id) {
    const auto list = wallets(params);
    if (id == selectedId(params) || std::none_of(list.begin(), list.end(), [&id](const Entry &e) { return e.id == id; })) {
        return;
    }
    m_selected[params.ticker] = id;
    scheduleSave();
    emit walletsChanged();
}

QList<CoinVault::Entry> CoinVault::wallets(const CoinParams &params) const {
    QList<Entry> list;
    for (const Entry &e : m_entries) {
        if (e.wallet->params() == params) list.append(e);
    }
    return list;
}

QList<CoinWallet *> CoinVault::allWallets() const {
    QList<CoinWallet *> list;
    for (const Entry &e : m_entries) list.append(e.wallet);
    return list;
}

QString CoinVault::nextName(const CoinParams &params) const {
    const auto list = wallets(params);
    for (int n = list.size() + 1;; ++n) {
        const QString name = QString("%1 %2").arg(params.name).arg(n);
        if (std::none_of(list.begin(), list.end(), [&name](const Entry &e) { return e.name == name; })) {
            return name;
        }
    }
}

std::optional<QByteArray> CoinVault::bip39Seed(const QString &id, QString *error) const {
    if (!m_session) {
        if (error) *error = "Bitcoin and Litecoin are locked";
        return std::nullopt;
    }
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    auto content = m_session->unseal(file.readAll());
    if (!content) {
        if (error) *error = "Unable to read the Bitcoin/Litecoin wallet file";
        return std::nullopt;
    }
    const QJsonObject obj = parseContent(*content);
    walletfile::wipe(*content);

    QJsonObject entry;
    if (id == mainId(coins::bitcoin()) || id == mainId(coins::litecoin())) {
        entry = obj;
    } else {
        for (const QJsonValue &value : obj.value("extra").toArray()) {
            if (value.toObject().value("id").toString() == id) {
                entry = value.toObject();
            }
        }
    }
    if (entry.isEmpty()) {
        if (error) *error = "Unknown Bitcoin/Litecoin wallet";
        return std::nullopt;
    }
    auto seed = bip39::mnemonicToSeed(entry.value("mnemonic").toString(), entry.value("passphrase").toString());
    if (!seed && error) *error = "Invalid seed in wallet file";
    return seed;
}

bool CoinVault::rewrite(const std::function<void(QJsonObject &)> &change, QString *error) {
    if (!m_session) {
        if (error) *error = "Bitcoin and Litecoin are locked";
        return false;
    }
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    auto content = m_session->unseal(file.readAll());
    file.close();
    if (!content) {
        if (error) *error = "Unable to read the Bitcoin/Litecoin wallet file";
        return false;
    }
    QJsonObject obj = parseContent(*content);
    walletfile::wipe(*content);
    change(obj);
    obj["version"] = fileVersion;
    QByteArray updated = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    obj = {};
    const bool saved = m_session->save(path(), updated, error);
    walletfile::wipe(updated);
    return saved;
}

bool CoinVault::addWallet(const CoinParams &params, const QString &name, const QString &mnemonic,
                          const QString &passphrase, QString *error) {
    if (!m_session) {
        if (error) *error = "Bitcoin and Litecoin are locked";
        return false;
    }
    if (!bip39::isValidMnemonic(mnemonic)) {
        if (error) *error = "Invalid seed phrase (12 or 24 BIP39 words)";
        return false;
    }
    const QString cleanName = name.trimmed().isEmpty() ? nextName(params) : name.trimmed();
    const auto seed = bip39::mnemonicToSeed(mnemonic, passphrase);
    if (!seed) {
        if (error) *error = "Invalid seed phrase";
        return false;
    }
    // Same keys as a wallet already here: nothing to add.
    auto account = HdAccount::fromSeed(*seed, params);
    const QString firstAddress = account ? account->address(HdAccount::Receive, 0) : QString();
    for (const Entry &e : wallets(params)) {
        if (e.wallet->firstAddress() == firstAddress) {
            if (error) *error = QString("This %1 wallet is already in Biscuit (\"%2\").").arg(params.name, e.name);
            return false;
        }
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString normalized = bip39::normalizeMnemonic(mnemonic);
    const bool written = rewrite([&](QJsonObject &obj) {
        QJsonArray extra = obj.value("extra").toArray();
        extra.append(QJsonObject{
            {"id", id}, {"name", cleanName}, {"coin", params.ticker},
            {"mnemonic", normalized}, {"passphrase", passphrase},
            {"created", created}, {"cache", QJsonObject{}},
        });
        obj["extra"] = extra;
    }, error);
    if (!written) {
        return false;
    }
    QByteArray seedBytes = *seed;
    const bool added = addEntry(id, cleanName, false, params, seedBytes, {}, error);
    walletfile::wipe(seedBytes);
    if (!added) {
        return false;
    }
    applyNetworkSettings();
    m_selected[params.ticker] = id;
    scheduleSave();
    emit walletsChanged();
    return true;
}

bool CoinVault::removeWallet(const QString &id, QString *error) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&id](const Entry &e) { return e.id == id; });
    if (it == m_entries.end() || it->mainSeed) {
        if (error) *error = "This wallet cannot be removed";
        return false;
    }
    const bool written = rewrite([&id](QJsonObject &obj) {
        QJsonArray kept;
        for (const QJsonValue &v : obj.value("extra").toArray()) {
            if (v.toObject().value("id").toString() != id) kept.append(v);
        }
        obj["extra"] = kept;
    }, error);
    if (!written) {
        return false;
    }
    const CoinParams &params = it->wallet->params();
    delete it->wallet;
    m_entries.erase(it);
    if (m_selected.value(params.ticker) == id) {
        m_selected[params.ticker] = mainId(params);
    }
    scheduleSave();
    emit walletsChanged();
    return true;
}

void CoinVault::applyNetworkSettings() {
    const int proxy = conf()->get(Config::proxy).toInt();
    QNetworkProxy networkProxy(QNetworkProxy::NoProxy);
    if (proxy != Config::Proxy::None) {
        // Same SOCKS5 proxy as the rest of the app (Tor managed by Biscuit or not).
        networkProxy = getNetworkSocks5()->proxy();
    }
    const bool onionOnly = proxy == Config::Proxy::Tor && conf()->get(Config::torOnlyAllowOnion).toBool();
    for (CoinWallet *w : allWallets()) {
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
    if (!m_session || m_entries.isEmpty()) {
        return;
    }
    // The seeds are read back with the session key (they are not kept in
    // memory); only the caches and the selection are replaced.
    QString error;
    const bool saved = rewrite([this](QJsonObject &obj) {
        QJsonObject caches;
        QHash<QString, QJsonObject> extraCaches;
        for (const Entry &e : m_entries) {
            if (e.mainSeed) {
                caches[e.wallet->params().ticker] = e.wallet->cache();
            } else {
                extraCaches.insert(e.id, e.wallet->cache());
            }
        }
        obj["coins"] = caches;
        QJsonArray extra = obj.value("extra").toArray();
        for (int i = 0; i < extra.size(); ++i) {
            QJsonObject entry = extra.at(i).toObject();
            const QString id = entry.value("id").toString();
            if (extraCaches.contains(id)) {
                entry["cache"] = extraCaches.value(id);
                extra[i] = entry;
            }
        }
        obj["extra"] = extra;
        QJsonObject selected;
        for (auto it = m_selected.cbegin(); it != m_selected.cend(); ++it) {
            selected[it.key()] = it.value();
        }
        obj["selected"] = selected;
    }, &error);
    if (!saved) {
        qWarning() << "Biscuit: unable to save the Bitcoin/Litecoin wallet file:" << error;
    }
}

}
