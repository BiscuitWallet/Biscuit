// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinSendController.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>

#include "Addresses.h"
#include "Amount.h"
#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "libwalletqt/Wallet.h"
#include "utils/Utils.h"

namespace biscuit::coins {

std::optional<CoinDestination> detectCoinDestination(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.contains('\n')) {
        return std::nullopt;
    }

    for (const CoinParams *params : {&bitcoin(), &litecoin()}) {
        // BIP21 URI: bitcoin:<address>?amount=<decimal>
        const QString scheme = params->name.toLower() + ':';
        if (trimmed.startsWith(scheme, Qt::CaseInsensitive)) {
            const QUrl url(trimmed);
            const QString address = url.path();
            if (isValidAddress(address, *params)) {
                const QString amount = QUrlQuery(url).queryItemValue("amount");
                return CoinDestination{params, address, biscuit::swap::amount::isValid(amount) ? amount : QString()};
            }
            return std::nullopt;
        }
        if (isValidSendDestination(trimmed, *params)) {
            return CoinDestination{params, trimmed, {}};
        }
    }
    return std::nullopt;
}

QList<FeeLevel> feeLevels() {
    return {
        {"Fast (~20 min)", 2},
        {"Normal (~1 hour)", 6},
        {"Slow (~4 hours)", 24},
    };
}

CoinSendController::CoinSendController(Wallet *wallet, QObject *parent)
    : QObject(parent)
    , m_wallet(wallet)
    , m_vault(CoinVault::forWallet(wallet))
{
}

std::optional<double> CoinSendController::feeRate(const CoinParams &params, int targetBlocks) const {
    if (!m_vault || !m_vault->isUnlocked()) {
        return std::nullopt;
    }
    return m_vault->wallet(params)->feeRate(targetBlocks);
}

bool CoinSendController::ensureReady(QWidget *parent) {
    if (!m_vault) {
        return false;
    }
    if (!m_vault->exists()) {
        QMessageBox box(parent);
        box.setWindowTitle("Bitcoin and Litecoin");
        box.setIcon(QMessageBox::Information);
        box.setText("Bitcoin and Litecoin are not set up in this wallet yet.");
        box.setInformativeText("They use one seed phrase (BIP39), separate from your Monero seed, "
                               "encrypted with the password of this wallet.");
        auto *create = box.addButton("Create new seed", QMessageBox::AcceptRole);
        auto *restore = box.addButton("Restore from seed", QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == create) {
            CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Create, parent).exec();
        } else if (box.clickedButton() == restore) {
            CoinSetupDialog(m_vault, CoinSetupDialog::Mode::Restore, parent).exec();
        }
        return false;   // the user sends again once the wallet is synchronized
    }
    if (!m_vault->isUnlocked()) {
        bool ok = false;
        const QString password = QInputDialog::getText(parent, "Bitcoin and Litecoin", "Password of this wallet:",
                                                       QLineEdit::Password, {}, &ok);
        if (!ok) {
            return false;
        }
        QString error;
        if (!m_vault->unlock(password, &error)) {
            Utils::showError(parent, "Unable to unlock", error);
        }
        return false;   // unlocking starts the synchronization
    }
    return true;
}

void CoinSendController::send(QWidget *parent, const CoinParams &params, const QString &address,
                              const QString &amountText, int targetBlocks) {
    if (!isValidSendDestination(address, params)) {
        Utils::showError(parent, "Invalid address", QString("This is not a valid %1 address.").arg(params.name));
        return;
    }
    const bool sendAll = amountText.trimmed() == QLatin1String("all");
    quint64 amount = 0;
    if (!sendAll) {
        QString text = amountText.trimmed();
        text.replace(',', '.');
        const auto parsed = biscuit::swap::amount::toAtomic(text, params.decimals);
        if (!parsed || *parsed == 0) {
            Utils::showError(parent, "Invalid amount", QString("Enter an amount in %1, with at most 8 decimals.").arg(params.ticker));
            return;
        }
        amount = *parsed;
    }
    if (!ensureReady(parent)) {
        return;
    }

    CoinWallet *coin = m_vault->wallet(params);
    if (coin->status() != CoinWallet::Status::Synchronized) {
        Utils::showError(parent, "Not synchronized", QString("Wait until %1 is synchronized, then try again.").arg(params.name));
        return;
    }

    const double rate = coin->feeRate(targetBlocks);
    QString error;
    const auto plan = coin->planSend(address, amount, rate, sendAll, &error);
    if (!plan) {
        Utils::showError(parent, "Unable to send", error);
        return;
    }

    auto format = [&params](quint64 sats) {
        return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(sats, params.decimals), params.ticker);
    };

    // Explicit confirmation: nothing is signed or sent before this.
    QMessageBox box(parent);
    box.setWindowTitle(QString("Send %1").arg(params.name));
    box.setIcon(QMessageBox::Question);
    box.setText(QString("Send %1?").arg(format(plan->amount)));
    // With several wallets of this coin, say which one pays.
    const QString from = m_vault->wallets(params).size() > 1
                         ? QString("From: %1 wallet \"%2\"\n").arg(params.ticker, m_vault->selectedName(params)) : QString();
    // A silent payment goes to a one-time address only the recipient can find.
    const QString to = plan->silentPaymentAddress.isEmpty() ? address
        : QString("%1\n(silent payment: a new address only the recipient can recognize, %2)")
              .arg(address, segwitAddress(plan->outputs.at(plan->changeOutput == 0 ? 1 : 0).scriptPubKey, params));
    box.setInformativeText(QString("%1To: %2\nNetwork fee: %3 (%4 sat/vB)\nTotal: %5")
                           .arg(from, to, format(plan->fee))
                           .arg(rate, 0, 'f', 1)
                           .arg(format(plan->amount + plan->fee)));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    QPointer<QWidget> guard(parent);
    coin->broadcast(*plan, [this, guard](const QString &txid, const QString &error) {
        if (!error.isEmpty()) {
            Utils::showError(guard, "Transaction not sent", error);
            return;
        }
        Utils::showInfo(guard, "Transaction sent", QString("Transaction ID: %1").arg(txid));
        emit sent(txid);
    });
}

}
