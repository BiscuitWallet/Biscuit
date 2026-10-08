// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINSENDCONTROLLER_H
#define BISCUIT_COINSENDCONTROLLER_H

#include <optional>

#include <QObject>
#include <QPointer>
#include <QString>

#include "CoinParams.h"

class QWidget;
class Wallet;

namespace biscuit::coins {

class CoinWallet;

class CoinVault;

// A destination typed or pasted in the Send tab, if it is Bitcoin, Litecoin
// or Ethereum: a plain address or a BIP21 URI ("bitcoin:<address>?amount=0.01").
struct CoinDestination {
    const CoinParams *params = nullptr;
    QString address;
    QString amount;   // from the URI, empty if none
};
std::optional<CoinDestination> detectCoinDestination(const QString &text);

struct FeeLevel {
    QString label;
    int targetBlocks;
};
// Labels follow the coin's block time (Bitcoin 10 min, Litecoin 2.5 min).
QList<FeeLevel> feeLevels(const CoinParams &params);

// Sends Bitcoin/Litecoin from the unified Send tab: checks the setup,
// plans the transaction, asks for explicit confirmation, broadcasts.
class CoinSendController : public QObject {
    Q_OBJECT

public:
    CoinSendController(Wallet *wallet, QObject *parent = nullptr);

    // Current fee rate for a target, if the coin wallet is available.
    std::optional<double> feeRate(const CoinParams &params, int targetBlocks) const;

    // `amountText` is a decimal amount or "all".
    void send(QWidget *parent, const CoinParams &params, const QString &address, const QString &amountText,
              int targetBlocks);

    // Ethereum: ETH, USDT or USDC from the selected Ethereum wallet. Asks the
    // node for the nonce and gas, shows everything, sends once confirmed.
    void sendEthereum(QWidget *parent, const QString &asset, const QString &address, const QString &amountText);
    // Network fee of a typical send of `asset` now, in ETH: about / at most.
    std::optional<QPair<QString, QString>> ethereumFee(const QString &asset) const;

    // Replace-by-fee: asks for a higher fee rate, shows the new fee, and
    // replaces the unconfirmed transaction after confirmation.
    void bumpFee(QWidget *parent, const CoinParams &params, CoinWallet *coin, const QString &txid);

    // True if this coin is in the wallet and unlocked. Otherwise offers to
    // add it or asks the password, and returns false: the coin may be ready
    // afterwards (check CoinVault::hasCoin), but not synchronized yet.
    // Also used by Swap to receive into this wallet.
    bool ensureReady(QWidget *parent, const CoinParams &params);

signals:
    void sent(const QString &txid);

private:

    QPointer<Wallet> m_wallet;
    QPointer<CoinVault> m_vault;
};

}

#endif // BISCUIT_COINSENDCONTROLLER_H
