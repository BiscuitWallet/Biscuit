// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ETH_H
#define BISCUIT_ETH_H

#include <memory>
#include <optional>

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

struct ext_key;

// Ethereum: keys, addresses, EIP-1559 transactions and ERC-20 transfers.
// No network, no UI: fully unit tested. Hashing with Monero's Keccak-256 and
// signing with libwally (secp256k1, RFC 6979), the same code as Bitcoin.
namespace biscuit::coins::eth {

// Amounts in wei or token units. 128 bits hold 3.4e38: far above the supply
// of ETH (~1.2e26 wei) or of any token used here. Larger values are refused.
using u128 = unsigned __int128;

constexpr quint64 mainnetChainId = 1;
constexpr int etherDecimals = 18;
constexpr quint64 transferGas = 21000;   // plain ETH transfer

QByteArray keccak256(const QByteArray &data);

// ---------------------------------------------------------------- addresses

// "0x…" with the EIP-55 checksum (mixed case).
QString checksumAddress(const QByteArray &address);
// 20 bytes from "0x…": all lowercase or all uppercase (no checksum), or the
// exact EIP-55 case. A mixed case that does not match is a typo: refused.
std::optional<QByteArray> parseAddress(const QString &text, QString *error = nullptr);
// Address of a secp256k1 public key (33 compressed or 65 uncompressed bytes).
QByteArray addressFromPublicKey(const QByteArray &publicKey);
// Address of a private key (32 bytes); empty if the key is invalid.
QByteArray addressFromPrivateKey(const QByteArray &privateKey);

// Keys from the BIP39 seed at m/44'/60'/0'/0/index, as MetaMask and most
// Ethereum wallets: the same seed gives the same addresses there.
class Account {
public:
    static std::optional<Account> fromSeed(const QByteArray &seed);

    Account(Account &&) noexcept;
    Account &operator=(Account &&) noexcept;
    ~Account();

    QByteArray address(quint32 index = 0) const;   // 20 bytes
    // Private key (32 bytes) for signing. Callers must wipe it after use.
    QByteArray privateKey(quint32 index = 0) const;

private:
    explicit Account(std::unique_ptr<ext_key> key);
    std::unique_ptr<ext_key> m_key;   // m/44'/60'/0'/0
};

// ------------------------------------------------------------------ amounts

// "1.5" with 18 decimals -> 1500000000000000000. Refuses signs, exponents,
// more decimals than the unit has, and values over 128 bits.
std::optional<u128> parseAmount(const QString &text, int decimals);
// 1500000000000000000 with 18 decimals -> "1.5" (no trailing zeros).
QString formatAmount(u128 value, int decimals);
// JSON-RPC quantity ("0x1bc1…", no leading zeros) -> value.
std::optional<u128> parseQuantity(const QString &hex);
QString toQuantity(u128 value);
// ABI uint256 (32 bytes, big-endian), as returned by eth_call.
std::optional<u128> parseUint256(const QByteArray &word);

// ---------------------------------------------------------------------- RLP

namespace rlp {
    QByteArray bytes(const QByteArray &value);
    QByteArray uint(u128 value);                  // minimal big-endian, 0 = empty
    QByteArray list(const QList<QByteArray> &encodedItems);

    struct Item {
        bool isList = false;
        QByteArray payload;   // string bytes, or the encoded items of a list
    };
    // Strict decoding (canonical lengths only) of one item filling `data`.
    std::optional<Item> decode(const QByteArray &data);
    // Items of an encoded list's payload.
    std::optional<QList<Item>> decodeItems(const QByteArray &payload);
}

// ------------------------------------------------------------- transactions

// EIP-1559 transaction (type 2), without access list.
struct Transaction {
    quint64 chainId = mainnetChainId;
    quint64 nonce = 0;
    u128 maxPriorityFeePerGas = 0;
    u128 maxFeePerGas = 0;
    quint64 gasLimit = 0;
    QByteArray to;      // 20 bytes
    u128 value = 0;
    QByteArray data;

    QByteArray unsignedSerialized() const;   // 0x02 || rlp([...])
    QByteArray signingHash() const;          // keccak256 of the above

    bool operator==(const Transaction &other) const;
};

// Signed raw transaction (0x02 || rlp([..., yParity, r, s])), ready for
// eth_sendRawTransaction.
std::optional<QByteArray> sign(const Transaction &tx, const QByteArray &privateKey, QString *error = nullptr);

struct SignedTransaction {
    Transaction tx;
    QByteArray from;   // 20 bytes, recovered from the signature
    QByteArray hash;   // transaction hash: keccak256(raw)
};
// Reads back a signed transaction: what will really be sent, and by whom.
std::optional<SignedTransaction> decode(const QByteArray &raw, QString *error = nullptr);

// ------------------------------------------------------------------ ERC-20

struct Token {
    QString symbol;
    QString name;
    QByteArray contract;   // 20 bytes
    int decimals;
};
// The tokens Biscuit handles, on Ethereum mainnet. A fixed list: tokens
// cannot be added, which rules out fake copies of USDT or USDC.
const QList<Token> &tokens();
const Token *tokenByContract(const QByteArray &contract);

QByteArray erc20TransferData(const QByteArray &to, u128 amount);
QByteArray erc20BalanceOfData(const QByteArray &owner);
// Recipient and amount of transfer() call data, if that is what it is.
std::optional<QPair<QByteArray, u128>> decodeErc20Transfer(const QByteArray &data);

}

#endif // BISCUIT_ETH_H
