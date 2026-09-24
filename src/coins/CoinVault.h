// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINVAULT_H
#define BISCUIT_COINVAULT_H

#include <memory>
#include <optional>

#include <QObject>
#include <QPointer>
#include <QTimer>

#include "CoinWallet.h"
#include "WalletFile.h"

class Wallet;

namespace biscuit::coins {

// The BTC/LTC side of a Monero wallet: a companion file "<wallet>.btcltc"
// next to the Monero wallet, encrypted with the same password.
//
// The seed words are never kept in memory: only the derived keys are. They
// are shown again only after the password is typed again (revealMnemonic).
class CoinVault : public QObject {
    Q_OBJECT

public:
    // One vault per open Monero wallet, destroyed with it.
    static CoinVault *forWallet(Wallet *wallet);

    QString path() const;
    bool exists() const;
    bool isUnlocked() const { return m_session.has_value(); }

    // Creates the file from a new or restored BIP39 seed. `password` must be
    // the Monero wallet password (checked).
    bool setUp(const QString &mnemonic, const QString &passphrase, const QString &password, QString *error);
    bool unlock(const QString &password, QString *error);
    void lock();

    // True if `password` opens the file (or there is no file).
    bool checkPassword(const QString &password) const;
    // Re-encrypts the file with a new password (new salt and key). Call after
    // the Monero wallet password was changed.
    bool changePassword(const QString &oldPassword, const QString &newPassword, QString *error);

    // Seed and optional passphrase, read from the file with the password.
    std::optional<QPair<QString, QString>> revealMnemonic(const QString &password, QString *error) const;

    CoinWallet *bitcoin() const { return m_btc; }
    CoinWallet *litecoin() const { return m_ltc; }
    CoinWallet *wallet(const CoinParams &params) const;

    // Applies the current proxy settings (Tor) to the Electrum connections.
    void applyNetworkSettings();

signals:
    void unlocked();
    void locked();

private:
    explicit CoinVault(Wallet *wallet);
    bool load(const QByteArray &content, QString *error);
    void scheduleSave();
    void save();

    QPointer<Wallet> m_wallet;
    std::optional<walletfile::Session> m_session;
    CoinWallet *m_btc = nullptr;
    CoinWallet *m_ltc = nullptr;
    QString m_created;
    QTimer m_saveTimer;
};

}

#endif // BISCUIT_COINVAULT_H
