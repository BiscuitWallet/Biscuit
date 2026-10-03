// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINVAULT_H
#define BISCUIT_COINVAULT_H

#include <functional>
#include <memory>
#include <optional>

#include <QHash>
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
// It holds the main BTC/LTC seed (one Bitcoin and one Litecoin wallet) and any
// other Bitcoin or Litecoin wallet the user added from its own seed. One
// wallet per coin is selected: Receive, Send, History and Swap use it.
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

    // Seed and optional passphrase, read from the file with the password: the
    // main seed, or with `id` the seed of a wallet added with addWallet.
    std::optional<QPair<QString, QString>> revealMnemonic(const QString &password, QString *error,
                                                          const QString &id = {}) const;
    // BIP39 seed (64 bytes) of one wallet, read from the file (vault
    // unlocked). For the atomic swap helper, which spends from that wallet.
    // The caller wipes it (walletfile::wipe) once handed over.
    std::optional<QByteArray> bip39Seed(const QString &id, QString *error) const;

    struct Entry {
        QString id;          // "main-BTC", "main-LTC" or a random id
        QString name;        // shown on the wallet buttons
        bool mainSeed = false;
        CoinWallet *wallet = nullptr;
    };

    // Selected wallet of each coin (nullptr while locked).
    CoinWallet *bitcoin() const { return wallet(coins::bitcoin()); }
    CoinWallet *litecoin() const { return wallet(coins::litecoin()); }
    CoinWallet *wallet(const CoinParams &params) const;
    QString selectedId(const CoinParams &params) const;
    QString selectedName(const CoinParams &params) const;
    void select(const CoinParams &params, const QString &id);

    // Every wallet of a coin, main seed first, then in the order added.
    QList<Entry> wallets(const CoinParams &params) const;
    QList<CoinWallet *> allWallets() const;
    // Suggested name for the next wallet of a coin, e.g. "Litecoin 2".
    QString nextName(const CoinParams &params) const;

    // Adds a Bitcoin or Litecoin wallet from its own BIP39 seed (vault
    // unlocked), and selects it. The seed is stored in the encrypted file.
    bool addWallet(const CoinParams &params, const QString &name, const QString &mnemonic,
                   const QString &passphrase, QString *error);
    // Removes an added wallet (never the main seed). Its seed is erased from
    // the file: the user needs their own backup to add it again.
    bool removeWallet(const QString &id, QString *error);
    // Renames a wallet, the main one included (its name is kept per coin).
    // Names are unique per coin.
    bool renameWallet(const QString &id, const QString &name, QString *error);

    // Address book for Bitcoin and Litecoin (Monero keeps Feather's), stored
    // in the encrypted file. Needs the vault unlocked.
    struct Contact {
        QString name;
        QString address;
    };
    QList<Contact> contacts(const CoinParams &params) const { return m_contacts.value(params.ticker); }
    bool setContacts(const CoinParams &params, const QList<Contact> &contacts, QString *error);

    // Coin control: the coins chosen in the Coins tab for the next send of
    // a coin (keys from CoinWallet::coinKey), for its selected wallet.
    // Cleared when another wallet is selected or the send is done.
    void setCoinSelection(const CoinParams &params, const QStringList &coinKeys);
    QStringList coinSelection(const CoinParams &params) const;

    // Applies the current proxy settings (Tor) to the Electrum connections.
    void applyNetworkSettings();

signals:
    void unlocked();
    void locked();
    void walletsChanged();    // added, removed or selection changed
    void walletUpdated();     // balance, history or height of any wallet
    void coinSelectionChanged();
    void contactsChanged();

private:
    explicit CoinVault(Wallet *wallet);
    bool load(const QByteArray &content, QString *error);
    bool addEntry(const QString &id, const QString &name, bool mainSeed, const CoinParams &params,
                  const QByteArray &seed, const QJsonObject &cache, QString *error);
    // Reads the file, lets `change` edit it, writes it back (vault unlocked).
    bool rewrite(const std::function<void(QJsonObject &)> &change, QString *error);
    void clearWallets();
    void scheduleSave();
    void save();

    QPointer<Wallet> m_wallet;
    std::optional<walletfile::Session> m_session;
    QList<Entry> m_entries;
    QHash<QString, QString> m_selected;   // ticker -> entry id
    struct CoinSelection { QString walletId; QStringList coinKeys; };
    QHash<QString, CoinSelection> m_coinSelection;   // ticker -> chosen coins
    QHash<QString, QList<Contact>> m_contacts;       // ticker -> address book
    QString m_created;
    QTimer m_saveTimer;
};

}

#endif // BISCUIT_COINVAULT_H
