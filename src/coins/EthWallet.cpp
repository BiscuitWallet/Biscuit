// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "EthWallet.h"

#include <algorithm>
#include <limits>
#include <memory>

#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrl>

#include "core/WalletFile.h"
#include "utils/Networking.h"
#include "utils/TorManager.h"
#include "utils/config.h"

namespace biscuit::coins {

using namespace eth;

namespace {
    constexpr int refreshIntervalMs = 30 * 1000;
    // First sync of a wallet: up to 10 pages of 50 per source (ETH, USDT, USDC).
    constexpr int deepSyncPages = 10;
    // A node asking for more than this per gas is wrong or lying: refused
    // (1000 gwei, 50 times the busiest days of 2026).
    constexpr u128 maxSaneFeePerGas = u128(1000) * 1000000000;

    QString historyKey(const HistoryEntry &e) {
        return QString::fromLatin1(e.hash.toHex()) + e.asset;
    }

    QString amountText(u128 value) { return formatAmount(value, 0); }

    QJsonObject txToJson(const Transaction &tx) {
        return {{"chain", QString::number(tx.chainId)}, {"nonce", QString::number(tx.nonce)},
                {"tip", amountText(tx.maxPriorityFeePerGas)}, {"max", amountText(tx.maxFeePerGas)},
                {"gas", QString::number(tx.gasLimit)}, {"to", QString::fromLatin1(tx.to.toHex())},
                {"value", amountText(tx.value)}, {"data", QString::fromLatin1(tx.data.toHex())}};
    }

    std::optional<Transaction> txFromJson(const QJsonObject &o) {
        Transaction tx;
        bool ok1 = false, ok2 = false, ok3 = false;
        tx.chainId = o.value("chain").toString().toULongLong(&ok1);
        tx.nonce = o.value("nonce").toString().toULongLong(&ok2);
        tx.gasLimit = o.value("gas").toString().toULongLong(&ok3);
        const auto tip = parseAmount(o.value("tip").toString(), 0);
        const auto max = parseAmount(o.value("max").toString(), 0);
        const auto value = parseAmount(o.value("value").toString(), 0);
        tx.to = QByteArray::fromHex(o.value("to").toString().toLatin1());
        tx.data = QByteArray::fromHex(o.value("data").toString().toLatin1());
        if (!ok1 || !ok2 || !ok3 || !tip || !max || !value || tx.to.size() != 20) {
            return std::nullopt;
        }
        tx.maxPriorityFeePerGas = *tip;
        tx.maxFeePerGas = *max;
        tx.value = *value;
        return tx;
    }

    // At least 12.5% more than `old` (nodes ask for 10%), and never less than `now`.
    u128 raised(u128 old, u128 now) {
        return std::max(old + old / 8 + 1, now);
    }

    QJsonObject toJson(const HistoryEntry &e) {
        return {{"hash", QString::fromLatin1(e.hash.toHex())}, {"asset", e.asset}, {"in", e.incoming},
                {"amount", amountText(e.amount)}, {"fee", amountText(e.fee)},
                {"party", QString::fromLatin1(e.counterparty.toHex())},
                {"time", e.time.isValid() ? e.time.toString(Qt::ISODate) : QString()},
                {"block", QString::number(e.block)}, {"failed", e.failed}};
    }

    std::optional<HistoryEntry> fromJson(const QJsonObject &o) {
        HistoryEntry e;
        e.hash = QByteArray::fromHex(o.value("hash").toString().toLatin1());
        e.asset = o.value("asset").toString();
        e.incoming = o.value("in").toBool();
        const auto amount = parseAmount(o.value("amount").toString(), 0);
        const auto fee = parseAmount(o.value("fee").toString(), 0);
        e.counterparty = QByteArray::fromHex(o.value("party").toString().toLatin1());
        e.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        e.block = o.value("block").toString().toULongLong();
        e.failed = o.value("failed").toBool();
        if (e.hash.size() != 32 || !EthWallet::assets().contains(e.asset) || !amount || !fee) {
            return std::nullopt;
        }
        e.amount = *amount;
        e.fee = *fee;
        return e;
    }
}

EthWallet::EthWallet(Account account, const QJsonObject &cache, QObject *parent)
    : QObject(parent)
    , m_account(std::move(account))
    , m_address(m_account.address(0))
{
    // Nodes are tried from a random one: not everybody on the same.
    m_node = int(QRandomGenerator::global()->bounded(quint32(defaultNodes().size())));

    const QJsonObject balances = cache.value("balances").toObject();
    for (const QString &asset : assets()) {
        if (const auto value = parseAmount(balances.value(asset).toString(), 0)) {
            m_balances.insert(asset, *value);
        }
    }
    for (const QJsonValue &v : cache.value("history").toArray()) {
        if (const auto e = fromJson(v.toObject())) m_history.insert(historyKey(*e), *e);
    }
    for (const QJsonValue &v : cache.value("sent").toArray()) {
        if (const auto e = fromJson(v.toObject())) m_sent.insert(historyKey(*e), *e);
    }
    m_historyComplete = cache.value("historyComplete").toBool();
    const QJsonObject pending = cache.value("pending").toObject();
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (const auto tx = txFromJson(it.value().toObject())) m_pending.insert(it.key(), *tx);
    }

    m_timer.setInterval(refreshIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &EthWallet::refresh);
    // Tor mode: start as soon as Tor is connected.
    connect(torManager(), &TorManager::connectionStateChanged, this, [this](bool connected) {
        if (connected && m_running) refresh();
    });
}

QString EthWallet::addressText() const {
    return checksumAddress(m_address);
}

QString EthWallet::nodeName() const {
    const QStringList list = nodes();
    return list.isEmpty() ? QString() : QUrl(list.at(m_node % list.size())).host();
}

QStringList EthWallet::assets() {
    QStringList list{"ETH"};
    for (const Token &t : tokens()) list << t.symbol;
    return list;
}

int EthWallet::decimals(const QString &asset) {
    for (const Token &t : tokens()) {
        if (t.symbol == asset) return t.decimals;
    }
    return etherDecimals;
}

QList<HistoryEntry> EthWallet::history() const {
    QList<HistoryEntry> list = m_history.values();
    for (const HistoryEntry &e : m_sent) list << e;
    std::sort(list.begin(), list.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        const quint64 ba = a.block ? a.block : std::numeric_limits<quint64>::max();
        const quint64 bb = b.block ? b.block : std::numeric_limits<quint64>::max();
        return ba != bb ? ba > bb : a.hash < b.hash;
    });
    return list;
}

void EthWallet::setCustomNode(const QString &url) {
    if (url.trimmed() != m_customNode) {
        m_customNode = url.trimmed();
        m_node = 0;
        if (m_running) refresh();
    }
}

void EthWallet::setCustomBlockscout(const QString &url) {
    m_customBlockscout = url.trimmed();
}

QStringList EthWallet::nodes() const {
    return m_customNode.isEmpty() ? defaultNodes() : QStringList{m_customNode};
}

QString EthWallet::blockscout() const {
    QString url = m_customBlockscout.isEmpty() ? defaultBlockscout() : m_customBlockscout;
    while (url.endsWith('/')) url.chop(1);
    return url;
}

bool EthWallet::networkAllowed() const {
    if (conf()->get(Config::offlineMode).toBool()) {
        return false;
    }
    if (conf()->get(Config::proxy).toInt() != Config::Proxy::Tor) {
        return true;
    }
    // Tor mode: wait for Tor. "Onion services only": no built-in node is one.
    const bool onionOnly = conf()->get(Config::torOnlyAllowOnion).toBool();
    return torManager()->torConnected && (!onionOnly || QUrl(m_customNode).host().endsWith(".onion"));
}

void EthWallet::setStatus(Status status) {
    if (status != m_status) {
        m_status = status;
        emit statusChanged(status);
    }
}

void EthWallet::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    m_timer.start();
    refresh();
}

void EthWallet::stop() {
    m_running = false;
    m_timer.stop();
    ++m_generation;
    setStatus(Status::Disconnected);
}

void EthWallet::refresh() {
    if (!m_running || !networkAllowed()) {
        return;
    }
    if (m_status == Status::Disconnected) {
        setStatus(Status::Connecting);
    }
    ++m_generation;
    refreshNode();
    refreshHistory();
}

// ------------------------------------------------------------------ network

void EthWallet::call(const QString &method, const QJsonArray &params, RpcDone done, int attempt) {
    const QStringList list = nodes();
    if (list.isEmpty() || !networkAllowed()) {
        done(std::nullopt, "Ethereum is offline");
        return;
    }
    const int node = m_node % list.size();
    const QString url = list.at(node);
    const int id = int(QRandomGenerator::global()->bounded(1, 1 << 30));
    QNetworkReply *reply = Networking(this).postJson(this, url, rpc::request(id, method, params));
    if (!reply) {
        done(std::nullopt, "Ethereum is offline");
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, method, params, done, attempt, list, node] {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && !doc.isObject()) {
            // This node is down or blocks us: the next one, once round. Calls made
            // together fail together: only the first moves on, the others follow it.
            if (m_node % list.size() == node) {
                m_node = (node + 1) % list.size();
            }
            if (attempt + 1 < list.size()) {
                call(method, params, done, attempt + 1);
            } else {
                done(std::nullopt, "No Ethereum node answered. Check your connection.");
            }
            return;
        }
        QString error;
        const auto result = rpc::result(doc.object(), id, &error);
        done(result, error);
    });
}

void EthWallet::fetchJson(const QString &url, std::function<void(std::optional<QJsonValue>)> done) {
    QNetworkReply *reply = Networking(this).getJson(this, url);
    if (!reply) {
        done(std::nullopt);
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        done(reply->error() == QNetworkReply::NoError && doc.isObject() ? std::optional<QJsonValue>(doc.object()) : std::nullopt);
    });
}

void EthWallet::refreshNode() {
    // Chain, balances and fees, all from the same node; applied together
    // once every answer is in.
    struct Pending {
        int remaining = 0;
        bool ok = true;
        QHash<QString, u128> balances;
        std::optional<Fees> fees;
    };
    auto state = std::make_shared<Pending>();
    const quint64 generation = m_generation;
    const QString me = addressText();
    const int node = m_node;
    auto finish = [this, state, generation, node] {
        if (--state->remaining > 0 || generation != m_generation) {
            return;
        }
        if (!state->ok || !state->fees) {
            // Next refresh: another node, unless a failed call already moved on.
            if (m_node == node) {
                m_node = (m_node + 1) % std::max<int>(1, nodes().size());
            }
            if (m_status != Status::Synchronized) setStatus(Status::Connecting);
            return;
        }
        m_balances = state->balances;
        m_fees = state->fees;
        setStatus(Status::Synchronized);
        emit updated();
        emit cacheChanged();
    };

    state->remaining = 4 + int(tokens().size());
    call("eth_getTransactionCount", {me, "latest"}, [this, state, finish, generation](auto result, auto) {
        const auto mined = result ? parseQuantity(result->toString()) : std::nullopt;
        if (mined && generation == m_generation && *mined <= std::numeric_limits<quint64>::max()) {
            prunePending(quint64(*mined));
        }
        finish();
    });
    call("eth_chainId", {}, [state, finish](auto result, auto) {
        state->ok = state->ok && result && result->toString() == toQuantity(mainnetChainId);   // never another chain
        finish();
    });
    call("eth_getBalance", {me, "latest"}, [state, finish](auto result, auto) {
        const auto value = result ? parseQuantity(result->toString()) : std::nullopt;
        if (value) state->balances.insert("ETH", *value); else state->ok = false;
        finish();
    });
    // Tips of the cheaper quarter of each block: enough to be included, and
    // nothing more is paid than needed (the base fee is the bulk anyway).
    call("eth_feeHistory", {"0x5", "latest", QJsonArray{25}}, [state, finish](auto result, auto) {
        state->fees = result ? parseFeeHistory(*result) : std::nullopt;
        finish();
    });
    for (const Token &token : tokens()) {
        const QJsonObject callObject{{"to", checksumAddress(token.contract)},
                                     {"data", "0x" + QString::fromLatin1(erc20BalanceOfData(m_address).toHex())}};
        const QString symbol = token.symbol;
        call("eth_call", {callObject, "latest"}, [state, finish, symbol](auto result, auto) {
            const QString hex = result ? result->toString() : QString();
            const auto value = hex.startsWith("0x") ? parseUint256(QByteArray::fromHex(hex.mid(2).toLatin1())) : std::nullopt;
            if (value) state->balances.insert(symbol, *value); else state->ok = false;
            finish();
        });
    }
}

void EthWallet::prunePending(quint64 minedNonce) {
    bool changed = false;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it.value().nonce >= minedNonce) {
            ++it;
            continue;
        }
        // Its nonce is used by a mined transaction: this one is in a block, or
        // was replaced (sped up) and will never be. Blockscout shows the real one.
        const QByteArray hash = QByteArray::fromHex(it.key().toLatin1());
        for (auto s = m_sent.begin(); s != m_sent.end();) {
            s = s.value().hash == hash ? m_sent.erase(s) : std::next(s);
        }
        it = m_pending.erase(it);
        changed = true;
    }
    if (changed) {
        emit updated();
        emit cacheChanged();
    }
}

bool EthWallet::historyAllowed() const {
    // "Onion services only": the history too, or not at all.
    return !(conf()->get(Config::proxy).toInt() == Config::Proxy::Tor && conf()->get(Config::torOnlyAllowOnion).toBool()
             && !QUrl(blockscout()).host().endsWith(".onion"));
}

void EthWallet::fetchPages(const QString &path, const QString &query, int pagesLeft,
                           std::function<void(const QJsonValue &)> page, std::function<void(bool)> done) {
    fetchJson(blockscout() + blockscout::withQuery(path, query), [this, path, pagesLeft, page, done](auto reply) {
        if (!reply || !m_running) {
            done(false);
            return;
        }
        page(*reply);
        const QString next = blockscout::nextPageQuery(*reply);
        if (next.isEmpty() || pagesLeft <= 1) {
            done(true);   // the oldest page, or enough of them
            return;
        }
        fetchPages(path, next, pagesLeft - 1, page, done);
    });
}

void EthWallet::startDeepSync() {
    struct Pending {
        int remaining = 0;
        bool ok = true;
        QList<HistoryEntry> entries;
        QHash<QByteArray, u128> fees;
    };
    auto state = std::make_shared<Pending>();
    m_deepSyncRunning = true;
    auto finish = [this, state](bool ok) {
        state->ok = state->ok && ok;
        if (--state->remaining > 0) {
            return;
        }
        m_deepSyncRunning = false;
        mergeHistory(state->entries, state->fees);
        if (state->ok) {
            m_historyComplete = true;   // otherwise: again at the next refresh
            emit cacheChanged();
        }
    };
    state->remaining = 1 + int(tokens().size());
    fetchPages(blockscout::transactionsPath(m_address), {}, deepSyncPages, [this, state](const QJsonValue &reply) {
        const auto list = blockscout::parseTransactions(reply, m_address, &state->fees);
        if (list) state->entries << *list; else state->ok = false;
    }, finish);
    for (const Token &token : tokens()) {
        fetchPages(blockscout::tokenTransfersPath(m_address, token), {}, deepSyncPages, [this, state, token](const QJsonValue &reply) {
            const auto list = blockscout::parseTokenTransfers(reply, m_address, token);
            if (list) state->entries << *list; else state->ok = false;
        }, finish);
    }
}

void EthWallet::refreshHistory() {
    if (!historyAllowed()) {
        return;
    }
    if (!m_historyComplete && !m_deepSyncRunning) {
        startDeepSync();
    }
    struct Pending {
        int remaining = 0;
        QList<HistoryEntry> entries;
        QHash<QByteArray, u128> fees;
    };
    auto state = std::make_shared<Pending>();
    const quint64 generation = m_generation;
    auto finish = [this, state, generation] {
        if (--state->remaining == 0 && generation == m_generation) {
            mergeHistory(state->entries, state->fees);
        }
    };
    state->remaining = 1 + int(tokens().size());
    fetchJson(blockscout() + blockscout::transactionsPath(m_address), [this, state, finish](auto reply) {
        if (reply) {
            if (const auto list = blockscout::parseTransactions(*reply, m_address, &state->fees)) state->entries << *list;
        }
        finish();
    });
    for (const Token &token : tokens()) {
        fetchJson(blockscout() + blockscout::tokenTransfersPath(m_address, token), [this, state, finish, token](auto reply) {
            if (reply) {
                if (const auto list = blockscout::parseTokenTransfers(*reply, m_address, token)) state->entries << *list;
            }
            finish();
        });
    }
}

void EthWallet::mergeHistory(const QList<HistoryEntry> &entries, const QHash<QByteArray, u128> &fees) {
    bool changed = false;
    for (HistoryEntry e : entries) {
        if (!e.incoming && e.fee == 0) {
            e.fee = fees.value(e.hash);   // token transfer: the fee of its transaction
        }
        const QString key = historyKey(e);
        changed = changed || m_sent.remove(key) > 0 || !m_history.contains(key)
                  || m_history.value(key).block != e.block || m_history.value(key).failed != e.failed;
        m_history.insert(key, e);
    }
    if (changed) {
        emit updated();
        emit cacheChanged();
    }
}

// --------------------------------------------------------------------- send

void EthWallet::planSend(const QString &asset, const QString &address, const QString &amountText,
                         std::function<void(std::optional<Plan>, const QString &)> done) {
    if (!assets().contains(asset)) {
        done(std::nullopt, "Unknown asset");
        return;
    }
    if (m_status != Status::Synchronized || !m_fees) {
        done(std::nullopt, "Wait until Ethereum is synchronized, then try again.");
        return;
    }
    QString error;
    const auto recipient = parseAddress(address, &error);
    if (!recipient) {
        done(std::nullopt, error);
        return;
    }
    if (tokenByContract(*recipient)) {
        done(std::nullopt, "This is the address of a token contract, not of a wallet: coins sent there are lost.");
        return;
    }
    const Token *token = nullptr;
    for (const Token &t : tokens()) {
        if (t.symbol == asset) token = &t;
    }
    const int units = decimals(asset);
    const bool all = amountText.trimmed().compare("all", Qt::CaseInsensitive) == 0;
    std::optional<u128> amount = all ? std::optional<u128>(0) : parseAmount(amountText, units);
    if (!amount || (!all && *amount == 0)) {
        done(std::nullopt, QString("Enter an amount in %1, with at most %2 decimals.").arg(asset).arg(units));
        return;
    }
    const Fees fees = *m_fees;
    if (fees.maxFeePerGas > maxSaneFeePerGas) {
        done(std::nullopt, "The network fee given by the Ethereum node is abnormally high. Try again later.");
        return;
    }

    Plan plan;
    plan.asset = asset;
    plan.recipient = *recipient;
    plan.amount = *amount;
    plan.tx.chainId = mainnetChainId;
    plan.tx.maxPriorityFeePerGas = fees.priorityFee;
    plan.tx.maxFeePerGas = fees.maxFeePerGas;
    if (token) {
        if (all) plan.amount = balance(asset);
        plan.tx.to = token->contract;
        plan.tx.data = erc20TransferData(*recipient, plan.amount);
    } else {
        plan.tx.to = *recipient;
        plan.tx.value = all ? 0 : plan.amount;   // "all": known once the fee is
    }

    // Nonce, then gas, from the node; then the balances decide.
    const QString me = addressText();
    call("eth_getTransactionCount", {me, "pending"}, [this, plan, all, token, me, done](auto result, const QString &err) mutable {
        const auto nonce = result ? parseQuantity(result->toString()) : std::nullopt;
        if (!nonce || *nonce > std::numeric_limits<quint64>::max()) {
            done(std::nullopt, err.isEmpty() ? QString("The Ethereum node gave no nonce.") : err);
            return;
        }
        plan.tx.nonce = quint64(*nonce);
        QJsonObject estimate{{"from", me}, {"to", checksumAddress(plan.tx.to)}, {"value", toQuantity(plan.tx.value)}};
        if (!plan.tx.data.isEmpty()) estimate["data"] = "0x" + QString::fromLatin1(plan.tx.data.toHex());
        call("eth_estimateGas", {estimate}, [this, plan, all, token, done](auto result, const QString &err) mutable {
            const auto gas = result ? parseQuantity(result->toString()) : std::nullopt;
            if (!gas || *gas < transferGas || *gas > 1000000) {
                done(std::nullopt, err.isEmpty() ? QString("The Ethereum node could not estimate the network fee.")
                                                 : QString("This transaction would fail: %1").arg(err));
                return;
            }
            // A plain transfer uses exactly 21000; anything else gets 20% of margin
            // (unused gas is not paid).
            plan.tx.gasLimit = *gas == transferGas ? transferGas : quint64(*gas * 6 / 5);
            // The fee ceiling: 2 x base fee + tip, enough for about six full
            // blocks. Sending all the ETH, or when the ETH is just enough, it
            // comes down to as little as 1.25 x base fee + tip (two full
            // blocks): almost nothing is left behind, and a send that waits
            // can be sped up.
            const u128 eth = balance("ETH");
            if (m_fees) {
                const u128 narrow = narrowFeePerGas();
                const u128 gasLimit = plan.tx.gasLimit;
                const u128 spent = token ? 0 : plan.amount;
                if (!token && all) {
                    plan.tx.maxFeePerGas = std::min(plan.tx.maxFeePerGas, narrow);
                } else if (spent + gasLimit * plan.tx.maxFeePerGas > eth && eth > spent && (eth - spent) / gasLimit >= narrow) {
                    plan.tx.maxFeePerGas = (eth - spent) / gasLimit;
                }
            }
            plan.maxFee = u128(plan.tx.gasLimit) * plan.tx.maxFeePerGas;
            const QString maxFeeText = formatAmount(plan.maxFee, etherDecimals);
            if (token) {
                if (plan.amount == 0 || plan.amount > balance(plan.asset)) {
                    done(std::nullopt, QString("Not enough %1 in this wallet.").arg(plan.asset));
                    return;
                }
                if (plan.maxFee > eth) {
                    done(std::nullopt, QString("Sending %1 costs a network fee paid in ETH: up to %2 ETH. "
                                               "This wallet has %3 ETH. Receive a little ETH first.")
                                       .arg(plan.asset, maxFeeText, formatAmount(eth, etherDecimals)));
                    return;
                }
            } else if (all) {
                if (eth <= plan.maxFee) {
                    done(std::nullopt, QString("Not enough ETH to pay the network fee (up to %1 ETH).").arg(maxFeeText));
                    return;
                }
                plan.amount = eth - plan.maxFee;
                plan.tx.value = plan.amount;
            } else if (plan.amount + plan.maxFee > eth) {
                done(std::nullopt, QString("Not enough ETH: %1 ETH plus a network fee of up to %2 ETH.")
                                   .arg(formatAmount(plan.amount, etherDecimals), maxFeeText));
                return;
            }
            done(plan, {});
        });
    });
}

void EthWallet::broadcast(const Plan &plan, std::function<void(const QString &, const QString &)> done) {
    QByteArray key = m_account.privateKey(0);
    QString error;
    const auto raw = sign(plan.tx, key, &error);
    walletfile::wipe(key);
    if (!raw) {
        done({}, error);
        return;
    }

    // The final check: the signed bytes, read back, say exactly what the user
    // confirmed, from this wallet, on Ethereum mainnet.
    const auto check = decode(*raw);
    bool matches = check && check->tx == plan.tx && check->from == m_address && check->tx.chainId == mainnetChainId;
    if (matches && plan.asset == "ETH") {
        matches = check->tx.to == plan.recipient && check->tx.value == plan.amount && check->tx.data.isEmpty();
    } else if (matches) {
        const auto transfer = decodeErc20Transfer(check->tx.data);
        const Token *token = tokenByContract(check->tx.to);
        matches = token && token->symbol == plan.asset && check->tx.value == 0 && transfer
                  && transfer->first == plan.recipient && transfer->second == plan.amount;
    }
    if (!matches) {
        done({}, "The signed transaction does not match what you confirmed. Nothing was sent.");
        return;
    }

    // To every node at once: the first that accepts it is enough.
    const QString hash = "0x" + QString::fromLatin1(check->hash.toHex());
    const QString rawHex = "0x" + QString::fromLatin1(raw->toHex());
    const QStringList list = nodes();
    struct Pending {
        int remaining = 0;
        bool answered = false;
        QString firstError;
    };
    auto state = std::make_shared<Pending>();
    state->remaining = int(list.size());
    HistoryEntry pending;
    pending.hash = check->hash;
    pending.asset = plan.asset;
    pending.amount = plan.amount;
    pending.fee = plan.maxFee;
    pending.counterparty = plan.recipient;
    for (const QString &url : list) {
        const int id = int(QRandomGenerator::global()->bounded(1, 1 << 30));
        QNetworkReply *reply = Networking(this).postJson(this, url, rpc::request(id, "eth_sendRawTransaction", {rawHex}));
        if (!reply) {
            --state->remaining;
            continue;
        }
        const Transaction tx = plan.tx;
        connect(reply, &QNetworkReply::finished, this, [this, reply, id, state, hash, pending, done, tx] {
            reply->deleteLater();
            QString err;
            const auto result = rpc::result(QJsonDocument::fromJson(reply->readAll()).object(), id, &err);
            // "already known": another node passed it on first.
            const bool accepted = (result && result->toString().compare(hash, Qt::CaseInsensitive) == 0)
                                  || err.contains("already known", Qt::CaseInsensitive);
            if (accepted && !state->answered) {
                state->answered = true;
                // Same nonce as a pending send: this one replaces it (speed up).
                for (auto it = m_pending.begin(); it != m_pending.end();) {
                    if (it.value().nonce != tx.nonce) {
                        ++it;
                        continue;
                    }
                    const QByteArray old = QByteArray::fromHex(it.key().toLatin1());
                    for (auto s = m_sent.begin(); s != m_sent.end();) {
                        s = s.value().hash == old ? m_sent.erase(s) : std::next(s);
                    }
                    it = m_pending.erase(it);
                }
                m_pending.insert(QString::fromLatin1(pending.hash.toHex()), tx);
                m_sent.insert(historyKey(pending), pending);
                emit updated();
                emit cacheChanged();
                done(hash, {});
                QTimer::singleShot(5000, this, &EthWallet::refresh);
            } else if (!accepted && state->firstError.isEmpty()) {
                state->firstError = err.isEmpty() ? reply->errorString() : err;
            }
            if (--state->remaining == 0 && !state->answered) {
                done({}, state->firstError.isEmpty() ? QString("No Ethereum node answered.") : state->firstError);
            }
        });
    }
    if (state->remaining == 0) {
        done({}, "Ethereum is offline");
    }
}

QJsonObject EthWallet::cache() const {
    QJsonObject balances;
    for (auto it = m_balances.cbegin(); it != m_balances.cend(); ++it) {
        balances.insert(it.key(), amountText(it.value()));
    }
    QJsonArray history, sent;
    for (const HistoryEntry &e : m_history) history.append(toJson(e));
    for (const HistoryEntry &e : m_sent) sent.append(toJson(e));
    QJsonObject pending;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) pending.insert(it.key(), txToJson(it.value()));
    return {{"balances", balances}, {"history", history}, {"sent", sent}, {"pending", pending},
            {"historyComplete", m_historyComplete}};
}

u128 EthWallet::narrowFeePerGas() const {
    if (!m_fees) {
        return 0;
    }
    return m_fees->baseFee + m_fees->baseFee / 4 + std::max(m_fees->priorityFee, minimumPriorityFee);
}

u128 EthWallet::sendableEther() const {
    const u128 reserve = u128(transferGas) * narrowFeePerGas();
    const u128 eth = balance("ETH");
    return m_fees && eth > reserve ? eth - reserve : 0;
}

bool EthWallet::canSpeedUp(const QByteArray &hash) const {
    return m_pending.contains(QString::fromLatin1(hash.toHex()));
}

std::optional<u128> EthWallet::pendingMaxFee(const QByteArray &hash) const {
    const auto it = m_pending.constFind(QString::fromLatin1(hash.toHex()));
    if (it == m_pending.cend()) {
        return std::nullopt;
    }
    return u128(it->gasLimit) * it->maxFeePerGas;
}

void EthWallet::planSpeedUp(const QByteArray &hash, std::function<void(std::optional<Plan>, const QString &)> done) {
    const auto it = m_pending.constFind(QString::fromLatin1(hash.toHex()));
    if (it == m_pending.cend()) {
        done(std::nullopt, "This transaction is no longer waiting: it is in a block, or was replaced.");
        return;
    }
    if (!m_fees) {
        done(std::nullopt, "Wait until Ethereum is synchronized, then try again.");
        return;
    }
    const Transaction old = *it;
    // Still waiting? Its nonce is free as long as no transaction of ours with
    // it is in a block.
    call("eth_getTransactionCount", {addressText(), "latest"}, [this, old, done](auto result, const QString &err) {
        const auto mined = result ? parseQuantity(result->toString()) : std::nullopt;
        if (!mined) {
            done(std::nullopt, err.isEmpty() ? QString("The Ethereum node did not answer.") : err);
            return;
        }
        if (*mined > old.nonce) {
            prunePending(quint64(*mined));
            done(std::nullopt, "This transaction is already in a block: nothing to speed up.");
            return;
        }
        const Fees fees = *m_fees;
        Plan plan;
        plan.tx = old;
        plan.tx.maxPriorityFeePerGas = raised(old.maxPriorityFeePerGas, fees.priorityFee);
        plan.tx.maxFeePerGas = raised(old.maxFeePerGas, 2 * fees.baseFee + plan.tx.maxPriorityFeePerGas);
        if (plan.tx.maxFeePerGas > maxSaneFeePerGas) {
            done(std::nullopt, "The network fee given by the Ethereum node is abnormally high. Try again later.");
            return;
        }
        plan.maxFee = u128(plan.tx.gasLimit) * plan.tx.maxFeePerGas;
        // What it sends, read from the transaction itself.
        if (const auto transfer = decodeErc20Transfer(old.data); transfer && tokenByContract(old.to)) {
            plan.asset = tokenByContract(old.to)->symbol;
            plan.recipient = transfer->first;
            plan.amount = transfer->second;
        } else {
            plan.asset = "ETH";
            plan.recipient = old.to;
            plan.amount = old.value;
        }
        // The balance of the last block: the waiting send is not taken from it yet.
        const u128 needed = plan.maxFee + (plan.asset == "ETH" ? plan.amount : 0);
        if (needed > balance("ETH")) {
            done(std::nullopt, QString("Not enough ETH for the higher fee (up to %1 ETH).").arg(formatAmount(plan.maxFee, etherDecimals)));
            return;
        }
        done(plan, {});
    });
}

}
