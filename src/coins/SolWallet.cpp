// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SolWallet.h"

#include <algorithm>
#include <limits>
#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrl>

#include "core/EthApi.h"
#include "utils/Networking.h"
#include "utils/TorManager.h"
#include "utils/config.h"

namespace biscuit::coins {

using namespace sol;
namespace rpc = eth::rpc;   // plain JSON-RPC 2.0, as Ethereum's

namespace {
    constexpr int refreshIntervalMs = 30 * 1000;
    // Signatures listed per refresh; the first sync goes further back, once.
    constexpr int recentSignatures = 20;
    constexpr int deepSyncSignatures = 200;
    // Transactions read per refresh (public nodes limit how many per second).
    constexpr int transactionsPerRefresh = 8;
    // Ignored signatures (spam, nothing for us) remembered at most.
    constexpr int maxIgnored = 500;
    const QJsonObject confirmed{{"commitment", "confirmed"}};

    QJsonObject toJson(const HistoryEntry &e) {
        return {{"sig", e.signature}, {"in", e.incoming}, {"amount", QString::number(e.amount)},
                {"fee", QString::number(e.fee)}, {"party", e.counterparty.isEmpty() ? QString() : addressText(e.counterparty)},
                {"time", e.time.isValid() ? e.time.toString(Qt::ISODate) : QString()},
                {"slot", QString::number(e.slot)}, {"failed", e.failed}};
    }

    std::optional<HistoryEntry> fromJson(const QJsonObject &o) {
        HistoryEntry e;
        bool ok1 = false, ok2 = false, ok3 = false;
        e.signature = o.value("sig").toString();
        e.incoming = o.value("in").toBool();
        e.amount = o.value("amount").toString().toULongLong(&ok1);
        e.fee = o.value("fee").toString().toULongLong(&ok2);
        e.slot = o.value("slot").toString().toULongLong(&ok3);
        const QString party = o.value("party").toString();
        if (!party.isEmpty()) {
            const auto key = parseAddress(party);
            if (!key) return std::nullopt;
            e.counterparty = *key;
        }
        e.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        e.failed = o.value("failed").toBool();
        const auto sig = fromBase58(e.signature);
        if (!ok1 || !ok2 || !ok3 || !sig || sig->size() != 64) {
            return std::nullopt;
        }
        return e;
    }
}

SolWallet::SolWallet(Account account, const QJsonObject &cache, QObject *parent)
    : QObject(parent)
    , m_account(std::move(account))
{
    m_node = int(QRandomGenerator::global()->bounded(quint32(defaultNodes().size())));
    bool ok = false;
    const quint64 balance = cache.value("balance").toString().toULongLong(&ok);
    if (ok) m_balance = balance;
    for (const QJsonValue &v : cache.value("history").toArray()) {
        if (const auto e = fromJson(v.toObject())) m_history.insert(e->signature, *e);
    }
    for (const QJsonValue &v : cache.value("sent").toArray()) {
        const QJsonObject o = v.toObject();
        const auto e = fromJson(o.value("entry").toObject());
        const quint64 last = o.value("lastValid").toString().toULongLong(&ok);
        if (e && ok) m_sent.insert(e->signature, {*e, last});
    }
    for (const QJsonValue &v : cache.value("ignored").toArray()) m_ignored.insert(v.toString());
    m_historyComplete = cache.value("historyComplete").toBool();

    m_timer.setInterval(refreshIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SolWallet::refresh);
    // Tor mode: start as soon as Tor is connected.
    connect(torManager(), &TorManager::connectionStateChanged, this, [this](bool connected) {
        if (connected && m_running) refresh();
    });
}

QString SolWallet::nodeName() const {
    const QStringList list = nodes();
    return list.isEmpty() ? QString() : QUrl(list.at(m_node % list.size())).host();
}

QList<HistoryEntry> SolWallet::history() const {
    QList<HistoryEntry> list = m_history.values();
    for (const Sent &s : m_sent) list << s.entry;
    std::sort(list.begin(), list.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        const quint64 sa = a.slot ? a.slot : std::numeric_limits<quint64>::max();
        const quint64 sb = b.slot ? b.slot : std::numeric_limits<quint64>::max();
        return sa != sb ? sa > sb : a.signature < b.signature;
    });
    return list;
}

std::optional<quint64> SolWallet::transferFee() const {
    if (!m_priorityFee) {
        return std::nullopt;
    }
    Transfer t;
    t.computeUnitLimit = transferComputeUnits;
    t.microLamportsPerUnit = *m_priorityFee;
    return t.fee();
}

quint64 SolWallet::sendable() const {
    const auto fee = transferFee();
    return fee && m_balance > *fee ? m_balance - *fee : 0;
}

void SolWallet::setCustomNode(const QString &url) {
    if (url.trimmed() != m_customNode) {
        m_customNode = url.trimmed();
        m_node = 0;
        if (m_running) refresh();
    }
}

QStringList SolWallet::nodes() const {
    return m_customNode.isEmpty() ? defaultNodes() : QStringList{m_customNode};
}

bool SolWallet::networkAllowed() const {
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

void SolWallet::setStatus(Status status) {
    if (status != m_status) {
        m_status = status;
        emit statusChanged(status);
    }
}

void SolWallet::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    m_timer.start();
    refresh();
}

void SolWallet::stop() {
    m_running = false;
    m_timer.stop();
    ++m_generation;
    setStatus(Status::Disconnected);
}

void SolWallet::refresh() {
    if (!m_running || !networkAllowed()) {
        return;
    }
    if (m_status == Status::Disconnected) {
        setStatus(Status::Connecting);
    }
    ++m_generation;
    refreshNode();
    refreshHistory();
    if (!m_sent.isEmpty()) {
        checkSent();
    }
}

// ------------------------------------------------------------------ network

void SolWallet::call(const QString &method, const QJsonArray &params, RpcDone done, int attempt) {
    const QStringList list = nodes();
    if (list.isEmpty() || !networkAllowed()) {
        done(std::nullopt, "Solana is offline");
        return;
    }
    const int node = m_node % list.size();
    const int id = int(QRandomGenerator::global()->bounded(1, 1 << 30));
    QNetworkReply *reply = Networking(this).postJson(this, list.at(node), rpc::request(id, method, params));
    if (!reply) {
        done(std::nullopt, "Solana is offline");
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, method, params, done, attempt, list, node] {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && !doc.isObject()) {
            // This node is down, limits us or blocks us: the next one, once round.
            if (m_node % list.size() == node) {
                m_node = (node + 1) % list.size();
            }
            if (attempt + 1 < list.size()) {
                call(method, params, done, attempt + 1);
            } else {
                done(std::nullopt, "No Solana node answered. Check your connection.");
            }
            return;
        }
        QString error;
        const auto result = rpc::result(doc.object(), id, &error);
        done(result, error);
    });
}

void SolWallet::refreshNode() {
    // Network, balance and fees, applied together once every answer is in.
    struct Pending {
        int remaining = 3;
        bool ok = true;
        std::optional<quint64> balance;
        std::optional<quint64> priorityFee;
    };
    auto state = std::make_shared<Pending>();
    const quint64 generation = m_generation;
    const int node = m_node;
    auto finish = [this, state, generation, node] {
        if (--state->remaining > 0 || generation != m_generation) {
            return;
        }
        if (!state->ok || !state->balance || !state->priorityFee) {
            if (m_node == node) {
                m_node = (m_node + 1) % std::max<int>(1, nodes().size());
            }
            if (m_status != Status::Synchronized) setStatus(Status::Connecting);
            return;
        }
        const bool changed = m_balance != *state->balance;
        m_balance = *state->balance;
        m_priorityFee = state->priorityFee;
        setStatus(Status::Synchronized);
        emit updated();
        if (changed) emit cacheChanged();
    };
    call("getGenesisHash", {}, [state, finish](auto result, auto) {
        state->ok = state->ok && result && result->toString() == mainnetGenesisHash;   // never another network
        finish();
    });
    call("getBalance", {addressText(), confirmed}, [state, finish](auto result, auto) {
        state->balance = result ? parseBalance(*result) : std::nullopt;
        finish();
    });
    // params is [[address]]. Not {QJsonArray{...}}: passed to call()'s const
    // QJsonArray &, those braces bind to the inner array, and the node refuses
    // ["address"] (then Solana never gets a fee, so never Synchronized).
    QJsonArray feeParams;
    feeParams.append(QJsonArray{addressText()});
    call("getRecentPrioritizationFees", feeParams, [state, finish](auto result, auto) {
        state->priorityFee = result ? parsePriorityFee(*result) : std::nullopt;
        finish();
    });
}

void SolWallet::refreshHistory() {
    if (m_fetching) {
        return;
    }
    // Newest first. The first sync lists further back, once.
    const int limit = m_historyComplete ? recentSignatures : deepSyncSignatures;
    m_fetching = true;
    call("getSignaturesForAddress", {addressText(), QJsonObject{{"limit", limit}, {"commitment", "confirmed"}}},
         [this](auto result, auto) {
        const auto list = result ? parseSignatures(*result) : std::nullopt;
        if (!list) {
            m_fetching = false;
            return;
        }
        QStringList unknown;
        bool changed = false;
        for (const SignatureInfo &s : *list) {
            if (m_sent.contains(s.signature) && !s.failed) {
                // Ours, now in a block: its details come with the transaction.
                unknown << s.signature;
            } else if (m_history.contains(s.signature)) {
                HistoryEntry &e = m_history[s.signature];
                if (e.slot != s.slot || e.failed != s.failed) {
                    e.slot = s.slot;
                    e.failed = s.failed;
                    changed = true;
                }
            } else if (!m_ignored.contains(s.signature) && !m_unreadable.contains(s.signature)) {
                unknown << s.signature;
            }
        }
        if (!m_historyComplete) {
            m_historyComplete = true;
            changed = true;
        }
        if (changed) {
            emit updated();
            emit cacheChanged();
        }
        fetchTransactions(unknown.mid(0, transactionsPerRefresh));
    });
}

void SolWallet::fetchTransactions(QStringList signatures) {
    if (signatures.isEmpty() || !m_running) {
        m_fetching = false;
        return;
    }
    const QString signature = signatures.takeFirst();
    // Versions 0 and 1 (2026) read the same way here.
    const QJsonObject options{{"encoding", "jsonParsed"}, {"maxSupportedTransactionVersion", 1}, {"commitment", "confirmed"}};
    call("getTransaction", {signature, options}, [this, signature, signatures](auto result, const QString &error) {
        // A newer version the node will only give to newer clients: not
        // asked again this session (the balance counts it all the same).
        if (!result && error.contains("maxSupportedTransactionVersion")) {
            m_unreadable.insert(signature);
        }
        const auto parsed = result && !result->isNull() ? parseTransaction(*result, address()) : std::nullopt;
        if (parsed && parsed->entry.signature == signature) {
            if (parsed->relevant) {
                m_history.insert(signature, parsed->entry);
                m_sent.remove(signature);
            } else if (m_ignored.size() < maxIgnored) {
                m_ignored.insert(signature);
            }
            emit updated();
            emit cacheChanged();
        }
        fetchTransactions(signatures);
    });
}

void SolWallet::checkSent() {
    // A send not in a block once its blockhash has expired never will be: it
    // failed, and nothing left the wallet.
    call("getBlockHeight", {confirmed}, [this](auto result, auto) {
        const double height = result && result->isDouble() ? result->toDouble() : 0;
        if (height <= 0) {
            return;
        }
        bool changed = false;
        for (auto it = m_sent.begin(); it != m_sent.end();) {
            if (quint64(height) <= it->lastValidBlockHeight || m_history.contains(it.key())) {
                ++it;
                continue;
            }
            HistoryEntry e = it->entry;
            e.failed = true;
            const QString sig = it.key();
            it = m_sent.erase(it);
            // Maybe it went through after all: the transaction tells.
            call("getSignatureStatuses", {QJsonArray{sig}, QJsonObject{{"searchTransactionHistory", true}}},
                 [this, e](auto result, auto) {
                const auto status = result ? parseSignatureStatus(*result) : std::nullopt;
                if (status && status->has_value() && !(*status)->failed) {
                    fetchTransactions({e.signature});
                    return;
                }
                if (status) {   // not seen, or failed
                    m_history.insert(e.signature, e);
                    emit updated();
                    emit cacheChanged();
                }
            });
            changed = true;
        }
        if (changed) {
            emit updated();
            emit cacheChanged();
        }
    });
}

// --------------------------------------------------------------------- send

void SolWallet::planSend(const QString &address, const QString &amountText,
                         std::function<void(std::optional<Plan>, const QString &)> done) {
    if (m_status != Status::Synchronized || !m_priorityFee) {
        done(std::nullopt, "Wait until Solana is synchronized, then try again.");
        return;
    }
    QString error;
    const auto recipient = parseAddress(address, &error);
    if (!recipient) {
        done(std::nullopt, error);
        return;
    }
    if (*recipient == this->address()) {
        done(std::nullopt, "This is the address of this wallet.");
        return;
    }
    const bool all = amountText.trimmed().compare("all", Qt::CaseInsensitive) == 0;
    const auto amount = all ? std::optional<quint64>(0) : parseAmount(amountText);
    if (!amount || (!all && *amount == 0)) {
        done(std::nullopt, QString("Enter an amount in SOL, with at most %1 decimals.").arg(decimals));
        return;
    }

    Plan plan;
    plan.recipient = *recipient;
    plan.amount = *amount;
    plan.transfer.from = this->address();
    plan.transfer.to = *recipient;
    plan.transfer.computeUnitLimit = transferComputeUnits;
    plan.transfer.microLamportsPerUnit = *m_priorityFee;
    plan.fee = plan.transfer.fee();

    // A recent blockhash (the transaction expires with it), then the
    // recipient's balance; then the balances decide.
    call("getLatestBlockhash", {confirmed}, [this, plan, all, done](auto result, const QString &err) mutable {
        const auto blockhash = result ? parseLatestBlockhash(*result) : std::nullopt;
        if (!blockhash) {
            done(std::nullopt, err.isEmpty() ? QString("The Solana node gave no recent block.") : err);
            return;
        }
        plan.transfer.recentBlockhash = blockhash->hash;
        plan.lastValidBlockHeight = blockhash->lastValidBlockHeight;
        call("getBalance", {sol::addressText(plan.recipient), confirmed}, [this, plan, all, done](auto result, const QString &err) mutable {
            const auto theirs = result ? parseBalance(*result) : std::nullopt;
            if (!theirs) {
                done(std::nullopt, err.isEmpty() ? QString("The Solana node did not answer.") : err);
                return;
            }
            const QString feeText = formatAmount(plan.fee);
            if (all) {
                if (m_balance <= plan.fee) {
                    done(std::nullopt, QString("Not enough SOL to pay the network fee (%1 SOL).").arg(feeText));
                    return;
                }
                plan.amount = m_balance - plan.fee;   // the wallet is left empty, which Solana allows
            } else if (plan.amount > m_balance || m_balance - plan.amount < plan.fee) {
                done(std::nullopt, QString("Not enough SOL: %1 SOL plus a network fee of %2 SOL.")
                                   .arg(formatAmount(plan.amount), feeText));
                return;
            } else if (const quint64 left = m_balance - plan.amount - plan.fee; left > 0 && left < rentExemptMinimum) {
                done(std::nullopt, QString("Solana keeps at least %1 SOL in an address that is not empty. Send at most "
                                           "%2 SOL, or all of it.")
                                   .arg(formatAmount(rentExemptMinimum),
                                        formatAmount(m_balance > rentExemptMinimum + plan.fee ? m_balance - rentExemptMinimum - plan.fee : 0)));
                return;
            }
            plan.emptyRecipient = *theirs == 0;
            if (plan.emptyRecipient && plan.amount < rentExemptMinimum) {
                done(std::nullopt, QString("This address holds no SOL: Solana needs at least %1 SOL to open it. Send at "
                                           "least that much.").arg(formatAmount(rentExemptMinimum)));
                return;
            }
            plan.transfer.lamports = plan.amount;
            done(plan, {});
        });
    });
}

void SolWallet::broadcast(const Plan &plan, std::function<void(const QString &, const QString &)> done) {
    QString error;
    const auto raw = sign(plan.transfer, m_account, &error);
    if (!raw) {
        done({}, error);
        return;
    }
    // The final check: the signed bytes, read back, say exactly what the user
    // confirmed, from this wallet.
    const auto check = decode(*raw);
    if (!check || !(check->transfer == plan.transfer) || check->transfer.from != address()
        || check->transfer.to != plan.recipient || check->transfer.lamports != plan.amount) {
        done({}, "The signed transaction does not match what you confirmed. Nothing was sent.");
        return;
    }

    // To every node at once: the first that accepts it is enough.
    const QString signature = toBase58(check->signature);
    const QString base64 = QString::fromLatin1(raw->toBase64());
    const QStringList list = nodes();
    struct Pending {
        int remaining = 0;
        bool answered = false;
        QString firstError;
    };
    auto state = std::make_shared<Pending>();
    state->remaining = int(list.size());
    Sent sent;
    sent.entry.signature = signature;
    sent.entry.amount = plan.amount;
    sent.entry.fee = plan.fee;
    sent.entry.counterparty = plan.recipient;
    sent.lastValidBlockHeight = plan.lastValidBlockHeight;
    const QJsonObject options{{"encoding", "base64"}, {"preflightCommitment", "confirmed"}};
    for (const QString &url : list) {
        const int id = int(QRandomGenerator::global()->bounded(1, 1 << 30));
        QNetworkReply *reply = Networking(this).postJson(this, url, rpc::request(id, "sendTransaction", {base64, options}));
        if (!reply) {
            --state->remaining;
            continue;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, id, state, signature, sent, done] {
            reply->deleteLater();
            QString err;
            const auto result = rpc::result(QJsonDocument::fromJson(reply->readAll()).object(), id, &err);
            // "already processed": another node passed it on first.
            const bool accepted = (result && result->toString() == signature)
                                  || err.contains("already been processed", Qt::CaseInsensitive);
            if (accepted && !state->answered) {
                state->answered = true;
                m_sent.insert(signature, sent);
                emit updated();
                emit cacheChanged();
                done(signature, {});
                QTimer::singleShot(5000, this, &SolWallet::refresh);
            } else if (!accepted && state->firstError.isEmpty()) {
                state->firstError = err.isEmpty() ? reply->errorString() : err;
            }
            if (--state->remaining == 0 && !state->answered) {
                done({}, state->firstError.isEmpty() ? QString("No Solana node answered.") : state->firstError);
            }
        });
    }
    if (state->remaining == 0) {
        done({}, "Solana is offline");
    }
}

QJsonObject SolWallet::cache() const {
    QJsonArray history, sent, ignored;
    for (const HistoryEntry &e : m_history) history.append(toJson(e));
    for (const Sent &s : m_sent) sent.append(QJsonObject{{"entry", toJson(s.entry)}, {"lastValid", QString::number(s.lastValidBlockHeight)}});
    for (const QString &s : m_ignored) ignored.append(s);
    return {{"balance", QString::number(m_balance)}, {"history", history}, {"sent", sent}, {"ignored", ignored},
            {"historyComplete", m_historyComplete}};
}

}
