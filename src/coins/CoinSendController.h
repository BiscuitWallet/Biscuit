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

// A destination typed or pasted in the Send tab, if it is Bitcoin or Litecoin:
// a plain address or a BIP21 URI ("bitcoin:<address>?amount=0.01").
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

    // Replace-by-fee: asks for a higher fee rate, shows the new fee, and
    // replaces the unconfirmed transaction after confirmation.
    void bumpFee(QWidget *parent, const CoinParams &params, CoinWallet *coin, const QString &txid);

    // Makes sure Bitcoin/Litecoin are set up and unlocked, asking the user if needed.
    // Also used by Swap to receive into this wallet.
    bool ensureReady(QWidget *parent);

signals:
    void sent(const QString &txid);

private:

    QPointer<Wallet> m_wallet;
    QPointer<CoinVault> m_vault;
};

}

#endif // BISCUIT_COINSENDCONTROLLER_H
