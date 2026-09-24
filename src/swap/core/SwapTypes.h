// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_TYPES_H
#define BISCUIT_SWAP_TYPES_H

#include <optional>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace biscuit::swap {

// A coin is always identified by ticker AND network: the same ticker exists on
// several networks (e.g. USDT on ERC20, TRC20…).
struct Asset {
    QString ticker;   // lowercase, e.g. "xmr"
    QString network;  // e.g. "Mainnet", "ERC20"

    bool operator==(const Asset &other) const {
        return ticker == other.ticker && network == other.network;
    }
    bool operator!=(const Asset &other) const { return !(*this == other); }
    bool isValid() const { return !ticker.isEmpty() && !network.isEmpty(); }
    QString displayName() const;
};

struct AssetInfo {
    Asset asset;
    QString name;      // e.g. "Monero"
    QString minimum;   // decimal string, may be empty
    QString maximum;   // decimal string, may be empty
    bool hasMemo = false;
};

// Exchange KYC rating as published by the aggregator.
// A = no KYC, B, C, D = funds may be held until KYC.
enum class KycRating {
    A = 0,
    B,
    C,
    D,
    Unknown
};

KycRating kycRatingFromString(const QString &value);
QString kycRatingToString(KycRating rating);
QString kycRatingDescription(KycRating rating);

// True if `rating` is at least as good as `minimum` (A is best).
// Unknown ratings never pass a filter.
bool kycRatingAtLeast(KycRating rating, KycRating minimum);

enum class RateType {
    Floating = 0,
    Fixed
};

struct QuoteRequest {
    Asset from;
    Asset to;
    QString amountFrom;           // decimal string
    RateType rateType = RateType::Floating;
    KycRating minKycRating = KycRating::C;
};

// One offer from one exchange.
// Deliberately contains no commission or partner revenue field: ranking must
// only depend on what the user receives (and the ETA).
struct Quote {
    QString providerId;           // our provider module, e.g. "trocador"
    QString exchange;             // exchange behind the offer, e.g. "ChangeNOW"
    QString rateId;               // id needed to create the trade
    Asset from;
    Asset to;
    QString amountFrom;           // decimal string
    QString amountTo;             // decimal string
    RateType rateType = RateType::Floating;
    KycRating kycRating = KycRating::Unknown;
    std::optional<int> etaMinutes;
};

struct TradeRequest {
    Quote quote;
    QString payoutAddress;        // where the user receives the coins
    QString payoutMemo;           // empty if none
    QString refundAddress;        // where the exchange refunds on failure
    QString refundMemo;           // empty if none
};

enum class TradeStatus {
    New = 0,
    Waiting,
    Confirming,
    Sending,
    PaidPartially,
    Finished,
    Failed,
    Expired,
    Halted,
    Refunded,
    Unknown
};

TradeStatus tradeStatusFromString(const QString &value);
QString tradeStatusToString(TradeStatus status);
QString tradeStatusDescription(TradeStatus status);

// No more status changes expected: polling stops.
bool isTerminal(TradeStatus status);

// The user should contact support with the trade identifiers.
bool needsSupport(TradeStatus status);

// Status updates only move forward; a stale or out-of-order poll result must
// not move a trade back (e.g. Finished -> Sending).
bool canTransition(TradeStatus from, TradeStatus to);

struct Trade {
    // Identifiers, kept locally: the aggregator deletes trades after a while.
    QString providerId;           // our provider module, e.g. "trocador"
    QString tradeId;              // aggregator trade id
    QString exchange;             // exchange name
    QString exchangeTradeId;      // trade id at the exchange
    QString exchangePassword;     // some exchanges require it for support
    QString supportUrl;

    Asset from;
    Asset to;
    QString amountFrom;
    QString amountTo;
    RateType rateType = RateType::Floating;
    KycRating kycRating = KycRating::Unknown;

    QString depositAddress;       // where the user sends the coins
    QString depositMemo;
    QString payoutAddress;
    QString payoutMemo;
    QString refundAddress;
    QString refundMemo;

    TradeStatus status = TradeStatus::New;
    QDateTime createdAt;
    QDateTime updatedAt;

    // Monero txid of the deposit when sent from Biscuit, empty otherwise.
    QString depositTxId;

    QJsonObject toJson() const;
    static std::optional<Trade> fromJson(const QJsonObject &obj);
};

// Applies a status update if allowed. Returns true if the trade changed.
bool applyStatus(Trade &trade, TradeStatus status, const QDateTime &now);

}

#endif // BISCUIT_SWAP_TYPES_H
