// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapManager.h"

#include <memory>

#include "DemoSwapProvider.h"
#include "QuoteRanking.h"
#include "RequestPolicy.h"
#include "TrocadorSwapProvider.h"
#include "libwalletqt/Wallet.h"
#include "utils/config.h"

namespace biscuit::swap {

namespace {
    // Timer tick only: each trade is checked on the partner-agreed schedule
    // (swap/core/RequestPolicy), not on every tick.
    constexpr int pollTickMs = 30 * 1000;
}

SwapManager::SwapManager(Wallet *wallet, QObject *parent)
    : QObject(parent)
    , m_wallet(wallet)
{
    const QString baseUrl = TrocadorSwapProvider::builtInBaseUrl();
    if (baseUrl.isEmpty()) {
        m_allProviders.append(new DemoSwapProvider(this));
    } else {
        m_allProviders.append(new TrocadorSwapProvider(baseUrl, TrocadorSwapProvider::builtInApiKey(), this));
    }

    m_history = SwapHistory::fromJson(m_wallet->getCacheAttribute(SwapHistory::walletAttribute).toUtf8());

    connect(&m_pollTimer, &QTimer::timeout, this, [this] { poll(); });
    m_pollTimer.start(pollTickMs);

    // Saving a synchronizing wallet is unsafe: retry once it is synchronized.
    connect(m_wallet, &Wallet::connectionStatusChanged, this, [this](int status) {
        if (status == Wallet::ConnectionStatus_Synchronized) {
            flushIfPossible();
        }
    });

    QTimer::singleShot(0, this, [this] { poll(); });
}

QList<SwapProvider *> SwapManager::providers() const {
    const QStringList disabled = conf()->get(Config::swapDisabledProviders).toStringList();
    QList<SwapProvider *> enabled;
    for (SwapProvider *p : m_allProviders) {
        if (!disabled.contains(p->id())) {
            enabled.append(p);
        }
    }
    return enabled;
}

SwapProvider *SwapManager::provider(const QString &id) const {
    // Existing trades keep being followed even if their provider was disabled.
    for (SwapProvider *p : m_allProviders) {
        if (p->id() == id) {
            return p;
        }
    }
    return nullptr;
}

bool SwapManager::demoMode() const {
    for (SwapProvider *p : m_allProviders) {
        if (p->isDemo()) {
            return true;
        }
    }
    return false;
}

KycRating SwapManager::minKycRating() const {
    const KycRating rating = kycRatingFromString(conf()->get(Config::swapMinKycRating).toString());
    return rating == KycRating::Unknown ? KycRating::C : rating;
}

void SwapManager::setMinKycRating(KycRating rating) {
    conf()->set(Config::swapMinKycRating, kycRatingToString(rating));
}

void SwapManager::requestQuotes(const QuoteRequest &request, QuotesCallback callback) {
    const QList<SwapProvider *> active = providers();
    if (active.isEmpty()) {
        callback({}, {"All swap providers are disabled in the settings"});
        return;
    }

    // Only the latest request answers: older answers are dropped.
    const int generation = ++m_quoteGeneration;

    struct Pending {
        int remaining;
        QList<Quote> quotes;
        QStringList errors;
    };
    auto pending = std::make_shared<Pending>(Pending{static_cast<int>(active.size()), {}, {}});

    for (SwapProvider *p : active) {
        const QString name = p->displayName();
        p->getQuotes(request, [this, generation, pending, callback, request, name](const QList<Quote> &quotes, const QString &error) {
            pending->quotes.append(quotes);
            if (!error.isEmpty()) {
                pending->errors.append(QString("%1: %2").arg(name, error));
            }
            if (--pending->remaining > 0 || generation != m_quoteGeneration) {
                return;
            }
            callback(rankQuotes(pending->quotes, request.minKycRating), pending->errors);
        });
    }
}

void SwapManager::validateAddress(const Asset &asset, const QString &address, SwapProvider::ValidationCallback callback) {
    const QList<SwapProvider *> active = providers();
    if (active.isEmpty()) {
        callback(std::nullopt, "No swap provider enabled");
        return;
    }
    active.first()->validateAddress(asset, address, std::move(callback));
}

void SwapManager::createTrade(const TradeRequest &request, SwapProvider::TradeCallback callback) {
    SwapProvider *p = provider(request.quote.providerId);
    if (!p) {
        callback(std::nullopt, "Unknown swap provider");
        return;
    }
    p->createTrade(request, [this, request, callback](const std::optional<Trade> &trade, const QString &error) {
        if (trade) {
            Trade t = *trade;
            // Keep what the user asked for if the API omits it.
            if (t.payoutAddress.isEmpty()) t.payoutAddress = request.payoutAddress;
            if (t.refundAddress.isEmpty()) t.refundAddress = request.refundAddress;
            if (t.kycRating == KycRating::Unknown) t.kycRating = request.quote.kycRating;
            if (!t.createdAt.isValid()) t.createdAt = QDateTime::currentDateTimeUtc();
            t.updatedAt = QDateTime::currentDateTimeUtc();
            m_history.upsert(t);
            save();
            emit tradesChanged();
            callback(t, error);
            return;
        }
        callback(trade, error);
    });
}

QList<Trade> SwapManager::trades() const {
    return m_history.trades();
}

const Trade *SwapManager::trade(const QString &providerId, const QString &tradeId) const {
    return m_history.find(providerId, tradeId);
}

void SwapManager::setDepositTx(const QString &providerId, const QString &tradeId, const QString &txid) {
    const Trade *existing = m_history.find(providerId, tradeId);
    if (!existing) {
        return;
    }
    Trade t = *existing;
    t.depositTxId = txid;
    t.updatedAt = QDateTime::currentDateTimeUtc();
    m_history.upsert(t);
    save();
    emit tradesChanged();
}

void SwapManager::refreshNow() {
    poll(true);
}

void SwapManager::poll(bool force) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Trade &t : m_history.tradesToPoll(now)) {
        SwapProvider *p = provider(t.providerId);
        if (!p) {
            continue;
        }
        // Real partners: slow schedule agreed with them. The offline demo
        // contacts nobody and follows every tick.
        const QString key = t.providerId + ':' + t.tradeId;
        if (!force && !p->isDemo()) {
            const auto last = m_lastCheck.constFind(key);
            const bool never = last == m_lastCheck.constEnd();
            if (!policy::isStatusCheckDue(t.createdAt.secsTo(now), never ? 0 : last->secsTo(now), never)) {
                continue;
            }
        }
        m_lastCheck.insert(key, now);
        p->getTradeStatus(t, [this](const std::optional<Trade> &update, const QString &) {
            // Errors are transient (network, Tor): the next poll retries.
            if (update) {
                mergeStatus(*update);
            }
        });
    }
}

void SwapManager::mergeStatus(const Trade &update) {
    const Trade *existing = m_history.find(update.providerId, update.tradeId);
    if (!existing) {
        return;
    }
    Trade t = *existing;
    if (!applyStatus(t, update.status, QDateTime::currentDateTimeUtc())) {
        return;
    }
    // Final amounts can differ from the quote (floating rate).
    if (!update.amountTo.isEmpty()) {
        t.amountTo = update.amountTo;
    }
    m_history.upsert(t);
    save();
    emit tradesChanged();
}

void SwapManager::save() {
    if (!m_wallet) {
        return;
    }
    m_wallet->setCacheAttribute(SwapHistory::walletAttribute, QString::fromUtf8(m_history.toJson()));
    m_dirty = true;
    flushIfPossible();
}

void SwapManager::flushIfPossible() {
    if (!m_dirty || !m_wallet || !m_wallet->isSynchronized()) {
        return;
    }
    m_wallet->storeSafer();
    m_dirty = false;
}

}
