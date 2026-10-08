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
    // 1: main seed only. 2: adds "extra" wallets and "selected". Later
    // optional key: "mainCoins" (without it, both Bitcoin and Litecoin).
    constexpr int fileVersion = 2;
    constexpr char mainName[] = "Main";

    QString mainId(const CoinParams &params) {
        return QString("main-%1").arg(params.ticker);
    }

    const CoinParams *paramsForTicker(const QString &ticker) {
        for (const CoinParams *params : walletCoins()) {
            if (params->ticker == ticker) return params;
        }
        return nullptr;
    }

    // First address of a coin for a seed, to recognise a wallet already here.
    QString firstAddressOf(const QByteArray &seed, const CoinParams &params) {
        if (params.ethereum) {
            const auto account = eth::Account::fromSeed(seed);
            return account ? eth::checksumAddress(account->address(0)) : QString();
        }
        const auto account = HdAccount::fromSeed(seed, params);
        return account ? account->address(HdAccount::Receive, 0) : QString();
    }
    constexpr int saveDelayMs = 3000;

    QHash<Wallet *, QPointer<CoinVault>> &vaults() {
        static QHash<Wallet *, QPointer<CoinVault>> map;
        return map;
    }

    QJsonObject parseContent(const QByteArray &content) {
        return QJsonDocument::fromJson(content).object();
    }

    QJsonArray tickers(const QStringList &list) {
        QJsonArray array;
        for (const QString &ticker : list) array.append(ticker);
        return array;
    }
}

const QList<const CoinParams *> &walletCoins() {
    static const QList<const CoinParams *> list{&coins::bitcoin(), &coins::litecoin(), &coins::ethereum()};
    return list;
}

const CoinParams *coinOfTicker(const QString &ticker) {
    for (const CoinParams *params : walletCoins()) {
        if (params->ticker == ticker) return params;
    }
    for (const eth::Token &token : eth::tokens()) {
        if (token.symbol == ticker) return &coins::ethereum();
    }
    return nullptr;
}

QString coinNames(const QList<const CoinParams *> &coins) {
    QStringList names;
    for (const CoinParams *params : coins) names << params->name;
    if (names.size() < 2) {
        return names.join(QString());
    }
    const QString last = names.takeLast();
    return QString("%1 and %2").arg(names.join(", "), last);
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

bool CoinVault::setUp(const QString &mnemonic, const QString &passphrase, const QString &password,
                      const QList<const CoinParams *> &coins, QString *error) {
    if (coins.isEmpty()) {
        if (error) *error = "No coin chosen";
        return false;
    }
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
        {"mainCoins", tickers([&coins] {
            QStringList list;
            for (const CoinParams *params : walletCoins()) {
                const bool chosen = std::any_of(coins.begin(), coins.end(), [params](const CoinParams *c) { return *c == *params; });
                if (chosen) list << params->ticker;
            }
            return list;
        }())},
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
    // Coins on the main seed. Files written before coins became optional
    // have no list: both Bitcoin and Litecoin.
    m_mainCoins.clear();
    const QJsonArray mainCoins = obj.contains("mainCoins") ? obj.value("mainCoins").toArray()
                                                           : QJsonArray{coins::bitcoin().ticker, coins::litecoin().ticker};
    for (const CoinParams *params : walletCoins()) {
        if (mainCoins.contains(params->ticker)) {
            m_mainCoins << params->ticker;
        }
    }
    // Tokens shown ("tokens": ["USDT", ...]), each added by the user.
    m_tokens.clear();
    for (const eth::Token &token : eth::tokens()) {
        if (obj.value("tokens").toArray().contains(token.symbol)) m_tokens << token.symbol;
    }
    // The main wallets' names, if renamed ("mainNames": {"BTC": ..., "LTC": ...}).
    const QJsonObject mainNames = obj.value("mainNames").toObject();
    for (const CoinParams *params : walletCoins()) {
        if (!m_mainCoins.contains(params->ticker)) {
            continue;
        }
        const QString name = mainNames.value(params->ticker).toString(mainName);
        ok = ok && addEntry(mainId(*params), name, true, *params, seedBytes,
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

    m_contacts.clear();
    const QJsonObject contacts = obj.value("contacts").toObject();
    for (auto it = contacts.begin(); it != contacts.end(); ++it) {
        for (const QJsonValue &c : it.value().toArray()) {
            m_contacts[it.key()].append({c.toObject().value("name").toString(), c.toObject().value("address").toString()});
        }
    }

    const QJsonObject selected = obj.value("selected").toObject();
    for (const CoinParams *params : walletCoins()) {
        const QString id = selected.value(params->ticker).toString();
        const auto list = wallets(*params);
        const bool known = std::any_of(list.begin(), list.end(), [&id](const Entry &e) { return e.id == id; });
        m_selected[params->ticker] = known ? id : list.isEmpty() ? mainId(*params) : list.first().id;
    }
    applyNetworkSettings();   // sets the proxy and starts the wallets
    return true;
}

bool CoinVault::addEntry(const QString &id, const QString &name, bool mainSeed, const CoinParams &params,
                         const QByteArray &seed, const QJsonObject &cache, QString *error) {
    if (params.ethereum) {
        auto account = eth::Account::fromSeed(seed);
        if (!account) {
            if (error) *error = "Unable to derive keys";
            return false;
        }
        auto *w = new EthWallet(std::move(*account), cache, this);
        connect(w, &EthWallet::cacheChanged, this, &CoinVault::scheduleSave);
        connect(w, &EthWallet::updated, this, &CoinVault::walletUpdated);
        connect(w, &EthWallet::statusChanged, this, &CoinVault::walletUpdated);
        m_entries.append({id, name, mainSeed, &params, nullptr, w});
        return true;
    }
    auto account = HdAccount::fromSeed(seed, params);
    if (!account) {
        if (error) *error = "Unable to derive keys";
        return false;
    }
    auto *w = new CoinWallet(std::move(*account), cache, this);
    connect(w, &CoinWallet::cacheChanged, this, &CoinVault::scheduleSave);
    connect(w, &CoinWallet::updated, this, &CoinVault::walletUpdated);
    connect(w, &CoinWallet::statusChanged, this, &CoinVault::walletUpdated);
    m_entries.append({id, name, mainSeed, &params, w, nullptr});
    return true;
}

void CoinVault::clearWallets() {
    for (const Entry &e : m_entries) {
        delete e.wallet;
        delete e.eth;
    }
    m_entries.clear();
    m_mainCoins.clear();
    m_tokens.clear();
    m_selected.clear();
    m_contacts.clear();   // they live in the encrypted file only
    m_coinSelection.clear();
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

std::optional<QPair<QString, QString>> CoinVault::revealMnemonic(const QString &password, QString *error,
                                                                 const QString &id) const {
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
    if (id.isEmpty() || id.startsWith("main-")) {
        return qMakePair(obj.value("mnemonic").toString(), obj.value("passphrase").toString());
    }
    for (const QJsonValue &value : obj.value("extra").toArray()) {
        const QJsonObject extra = value.toObject();
        if (extra.value("id").toString() == id) {
            return qMakePair(extra.value("mnemonic").toString(), extra.value("passphrase").toString());
        }
    }
    if (error) *error = "This wallet is no longer in the file.";
    return std::nullopt;
}

CoinVault::CoinState CoinVault::coinState(const CoinParams &params) const {
    if (!exists()) {
        return CoinState::NotAdded;
    }
    if (!isUnlocked()) {
        return CoinState::Locked;
    }
    return wallets(params).isEmpty() ? CoinState::NotAdded : CoinState::Ready;
}

CoinVault::CoinState CoinVault::assetState(const QString &ticker) const {
    const CoinParams *params = coinOfTicker(ticker);
    if (!params) {
        return CoinState::NotAdded;
    }
    const CoinState state = coinState(*params);
    if (params->ticker == ticker || state != CoinState::Ready) {
        return state;
    }
    return m_tokens.contains(ticker) ? CoinState::Ready : CoinState::NotAdded;   // a token on Ethereum
}

bool CoinVault::setTokenEnabled(const QString &symbol, bool enabled, QString *error) {
    if (!m_session) {
        if (error) *error = "This wallet is locked";
        return false;
    }
    if (enabled && !hasCoin(coins::ethereum())) {
        if (error) *error = "Add Ethereum first";
        return false;
    }
    QStringList tokens;
    for (const eth::Token &token : eth::tokens()) {
        const bool on = token.symbol == symbol ? enabled : m_tokens.contains(token.symbol);
        if (on) tokens << token.symbol;
    }
    if (tokens == m_tokens) {
        return true;
    }
    if (!rewrite([&tokens](QJsonObject &obj) { obj["tokens"] = QJsonArray::fromStringList(tokens); }, error)) {
        return false;
    }
    m_tokens = tokens;
    emit walletsChanged();
    return true;
}

QList<const CoinParams *> CoinVault::mainSeedCoins() const {
    QList<const CoinParams *> list;
    for (const CoinParams *params : walletCoins()) {
        if (!isUnlocked() || m_mainCoins.contains(params->ticker)) list << params;
    }
    return list;
}

QString CoinVault::mainSeedCoinsText() const {
    return coinNames(mainSeedCoins());
}

bool CoinVault::addCoin(const CoinParams &params, QString *error) {
    if (!m_session) {
        if (error) *error = "Bitcoin and Litecoin are locked";
        return false;
    }
    if (coinState(params) == CoinState::Ready) {
        if (error) *error = QString("%1 is already in this wallet.").arg(params.name);
        return false;
    }
    auto seed = bip39Seed(mainId(coins::bitcoin()), error);   // the main seed
    if (!seed) {
        return false;
    }
    QStringList mainCoins;
    for (const CoinParams *p : walletCoins()) {
        if (m_mainCoins.contains(p->ticker) || *p == params) mainCoins << p->ticker;
    }
    QString name = mainName;
    const bool written = rewrite([&](QJsonObject &obj) {
        name = obj.value("mainNames").toObject().value(params.ticker).toString(mainName);
        obj["mainCoins"] = tickers(mainCoins);
    }, error);
    const bool added = written && addEntry(mainId(params), name, true, params, *seed, {}, error);
    walletfile::wipe(*seed);
    if (!added) {
        return false;
    }
    m_mainCoins = mainCoins;
    m_selected[params.ticker] = mainId(params);
    applyNetworkSettings();   // starts it
    scheduleSave();
    emit walletsChanged();
    return true;
}

CoinWallet *CoinVault::wallet(const CoinParams &params) const {
    const QString id = selectedId(params);
    for (const Entry &e : m_entries) {
        if (e.id == id) return e.wallet;
    }
    return nullptr;
}

EthWallet *CoinVault::ethereum() const {
    const QString id = selectedId(coins::ethereum());
    for (const Entry &e : m_entries) {
        if (e.id == id) return e.eth;
    }
    return nullptr;
}

eth::u128 CoinVault::ethereumBalance(const QString &asset) const {
    eth::u128 total = 0;
    for (const Entry &e : m_entries) {
        if (e.eth) total += e.eth->balance(asset);
    }
    return total;
}

QString CoinVault::Entry::firstAddress() const {
    return eth ? eth->addressText() : wallet ? wallet->firstAddress() : QString();
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
        if (*e.params == params) list.append(e);
    }
    return list;
}

QList<CoinWallet *> CoinVault::allWallets() const {
    QList<CoinWallet *> list;
    for (const Entry &e : m_entries) {
        if (e.wallet) list.append(e.wallet);
    }
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
    if (id.startsWith("main-")) {   // every coin of the main seed
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
    const QString firstAddress = firstAddressOf(*seed, params);
    for (const Entry &e : wallets(params)) {
        if (e.firstAddress() == firstAddress) {
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
    if (it == m_entries.end() || !m_session) {
        if (error) *error = "This wallet is no longer in Biscuit";
        return false;
    }
    if (it->wallet && !it->wallet->spendLock().isEmpty()) {
        if (error) *error = it->wallet->spendLock();   // an atomic swap uses it
        return false;
    }

    // The last one: nothing is left to keep, the file goes.
    if (m_entries.size() == 1) {
        m_saveTimer.stop();
        if (!QFile::remove(path())) {
            if (error) *error = "Unable to delete the wallet file";
            return false;
        }
        clearWallets();
        m_session.reset();   // wipes the key
        emit walletsChanged();
        return true;
    }

    const CoinParams &params = *it->params;
    const bool mainSeed = it->mainSeed;
    QStringList mainCoins = m_mainCoins;
    mainCoins.removeAll(params.ticker);
    // The last Ethereum wallet: its tokens go with it.
    const bool lastOfCoin = wallets(params).size() == 1;
    const bool dropTokens = params.ethereum && lastOfCoin;
    const bool written = rewrite([&](QJsonObject &obj) {
        if (dropTokens) {
            obj["tokens"] = QJsonArray();
        }
        if (mainSeed) {
            obj["mainCoins"] = tickers(mainCoins);
            QJsonObject caches = obj.value("coins").toObject();
            caches.remove(params.ticker);
            obj["coins"] = caches;
            return;
        }
        QJsonArray kept;
        for (const QJsonValue &v : obj.value("extra").toArray()) {
            if (v.toObject().value("id").toString() != id) kept.append(v);
        }
        obj["extra"] = kept;
    }, error);
    if (!written) {
        return false;
    }
    delete it->wallet;
    delete it->eth;
    m_entries.erase(it);
    if (mainSeed) {
        m_mainCoins = mainCoins;
    }
    if (dropTokens) {
        m_tokens.clear();
    }
    if (m_selected.value(params.ticker) == id) {
        const auto rest = wallets(params);
        m_selected[params.ticker] = rest.isEmpty() ? mainId(params) : rest.first().id;
        m_coinSelection.remove(params.ticker);
    }
    scheduleSave();
    emit walletsChanged();
    return true;
}

bool CoinVault::renameWallet(const QString &id, const QString &name, QString *error) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&id](const Entry &e) { return e.id == id; });
    if (it == m_entries.end()) {
        if (error) *error = "This wallet is no longer in Biscuit";
        return false;
    }
    const QString clean = name.simplified();
    if (clean.isEmpty()) {
        if (error) *error = "The name cannot be empty.";
        return false;
    }
    if (clean.size() > 32) {
        if (error) *error = "The name is too long (32 characters at most).";
        return false;
    }
    const CoinParams &params = *it->params;
    for (const Entry &e : wallets(params)) {
        if (e.id != id && e.name.compare(clean, Qt::CaseInsensitive) == 0) {
            if (error) *error = QString("Another %1 wallet is already called \"%2\".").arg(params.name, e.name);
            return false;
        }
    }
    const bool mainSeed = it->mainSeed;
    const QString ticker = params.ticker;
    const bool written = rewrite([&](QJsonObject &obj) {
        if (mainSeed) {
            QJsonObject names = obj.value("mainNames").toObject();
            names[ticker] = clean;
            obj["mainNames"] = names;
            return;
        }
        QJsonArray extra = obj.value("extra").toArray();
        for (int i = 0; i < extra.size(); ++i) {
            QJsonObject entry = extra.at(i).toObject();
            if (entry.value("id").toString() == id) {
                entry["name"] = clean;
                extra[i] = entry;
            }
        }
        obj["extra"] = extra;
    }, error);
    if (!written) {
        return false;
    }
    it->name = clean;
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
        // The user's own server, if set: then only that one is used.
        const auto key = w->params() == coins::bitcoin() ? Config::electrumServerBTC : Config::electrumServerLTC;
        const auto server = ElectrumServer::parse(conf()->get(key).toString()).value_or(ElectrumServer{{}, 0, true});
        // No onion Electrum server is built in yet: in onion-only mode the
        // wallets stay offline unless the user's server is an onion one.
        if (onionOnly && !server.isOnion()) {
            w->stop();
            continue;
        }
        w->setConnection(networkProxy, server);   // reconnects if anything changed
        if (!w->isRunning()) w->start();
    }
    // Ethereum follows the same settings by itself (Tor, offline mode).
    for (const Entry &e : m_entries) {
        if (e.eth) e.eth->isRunning() ? e.eth->refresh() : e.eth->start();
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
            const QJsonObject cache = e.eth ? e.eth->cache() : e.wallet->cache();
            if (e.mainSeed) {
                caches[e.params->ticker] = cache;
            } else {
                extraCaches.insert(e.id, cache);
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

void CoinVault::setCoinSelection(const CoinParams &params, const QStringList &coinKeys) {
    if (coinKeys.isEmpty()) {
        m_coinSelection.remove(params.ticker);
    } else {
        m_coinSelection.insert(params.ticker, {selectedId(params), coinKeys});
    }
    emit coinSelectionChanged();
}

QStringList CoinVault::coinSelection(const CoinParams &params) const {
    const auto it = m_coinSelection.constFind(params.ticker);
    if (it == m_coinSelection.cend() || it->walletId != selectedId(params)) {
        return {};   // chosen in another wallet of the coin
    }
    return it->coinKeys;
}

bool CoinVault::setContacts(const CoinParams &params, const QList<Contact> &contacts, QString *error) {
    const bool saved = rewrite([&params, &contacts](QJsonObject &obj) {
        QJsonArray list;
        for (const Contact &c : contacts) {
            list.append(QJsonObject{{"name", c.name}, {"address", c.address}});
        }
        QJsonObject all = obj.value("contacts").toObject();
        all[params.ticker] = list;
        obj["contacts"] = all;
    }, error);
    if (saved) {
        m_contacts[params.ticker] = contacts;
        emit contactsChanged();
    }
    return saved;
}

}
