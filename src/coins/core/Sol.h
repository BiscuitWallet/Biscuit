// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SOL_H
#define BISCUIT_SOL_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>

// Solana: keys, addresses and SOL transfers. No tokens: Biscuit's USDT and
// USDC are on Arbitrum. No network, no UI: fully unit tested. Ed25519 with
// libsodium (already used by Monero), base58 and HMAC-SHA512 with libwally.
namespace biscuit::coins::sol {

constexpr int decimals = 9;                   // 1 SOL = 10^9 lamports
constexpr quint64 signatureFee = 5000;        // lamports per signature
// The least a plain account may hold, other than nothing (rent exemption of
// an account without data): a send that would leave less fails.
constexpr quint64 rentExemptMinimum = 890880;
// Of Solana's main network: the node must be on it (as eth_chainId).
inline const QString mainnetGenesisHash = QStringLiteral("5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d");

QString toBase58(const QByteArray &data);
std::optional<QByteArray> fromBase58(const QString &text);

// ---------------------------------------------------------------- addresses

// An address is an Ed25519 public key (32 bytes), in base58.
QString addressText(const QByteArray &publicKey);
std::optional<QByteArray> parseAddress(const QString &text, QString *error = nullptr);

// The key at m/44'/501'/0'/0' of the BIP39 seed (SLIP-10, Ed25519), as
// Phantom, Solflare and most Solana wallets: the same seed gives the same
// address there. The secret never leaves this object: it signs.
class Account {
public:
    static std::optional<Account> fromSeed(const QByteArray &seed);
    // SLIP-10 Ed25519 key and chain code at a path of hardened indexes (the
    // `hardened` bit is added); for the tests.
    static QByteArray slip10(const QByteArray &seed, const QList<quint32> &path, QByteArray *chainCode = nullptr);

    Account(Account &&) noexcept;
    Account &operator=(Account &&) noexcept;
    ~Account();

    QByteArray publicKey() const { return m_public; }   // 32 bytes: the address
    QByteArray sign(const QByteArray &message) const;   // 64 bytes

private:
    Account() = default;
    QByteArray m_public;
    QByteArray m_secret;   // libsodium's 64 bytes (seed + public key), wiped
};

// ------------------------------------------------------------------ amounts

// "1.5" -> 1500000000 lamports. Refuses signs, exponents, more than 9
// decimals and values over 64 bits.
std::optional<quint64> parseAmount(const QString &text);
QString formatAmount(quint64 lamports);   // 1500000000 -> "1.5"

// ------------------------------------------------------------- transactions

// A SOL transfer with its priority fee: SetComputeUnitLimit,
// SetComputeUnitPrice, then the System Program's Transfer.
struct Transfer {
    QByteArray from;              // 32 bytes, the fee payer and only signer
    QByteArray to;                // 32 bytes, never `from`
    quint64 lamports = 0;
    QByteArray recentBlockhash;   // 32 bytes: the transaction expires with it
    quint32 computeUnitLimit = 0;
    quint64 microLamportsPerUnit = 0;   // priority fee

    QByteArray message() const;   // what is signed (legacy message)
    // Signature fee + priority fee (limit x price, rounded up), in lamports.
    quint64 fee() const;

    bool operator==(const Transfer &other) const;
};
// Compute units Biscuit asks for: the three instructions use 450.
constexpr quint32 transferComputeUnits = 1000;

// Signed transaction, ready for sendTransaction (in base64).
std::optional<QByteArray> sign(const Transfer &transfer, const Account &account, QString *error = nullptr);

struct SignedTransfer {
    Transfer transfer;
    QByteArray signature;   // 64 bytes; in base58, the transaction id
};
// Reads back a signed transaction: only this exact kind of transfer, with a
// valid signature of its sender. What will really be sent.
std::optional<SignedTransfer> decode(const QByteArray &raw, QString *error = nullptr);

}

#endif // BISCUIT_SOL_H
