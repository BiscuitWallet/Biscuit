// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinSendController.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>

#include "Addresses.h"
#include "Amount.h"
#include "CoinSetupDialog.h"
#include "CoinVault.h"
#include "CoinWallet.h"
#include "libwalletqt/Wallet.h"
#include "utils/AppData.h"
#include "utils/Utils.h"
#include "utils/config.h"

namespace biscuit::coins {

QString fiatAmount(const QString &ticker, const QString &amount) {
    const QString fiat = conf()->get(Config::preferredFiatCurrency).toString();
    if (!appData()->prices.canConvert(ticker, fiat)) {
        return {};
    }
    const double value = appData()->prices.convert(ticker, fiat, amount.toDouble());
    // Below a cent, say so rather than "€0.00".
    return value > 0 && value < 0.01 ? QString("less than %1").arg(Utils::amountToCurrencyString(0.01, fiat))
                                     : Utils::amountToCurrencyString(value, fiat);
}

QString fiatValue(const QString &ticker, const QString &amount) {
    const QString value = fiatAmount(ticker, amount);
    return value.isEmpty() ? QString() : QString(" ≈ %1").arg(value);
}

QString shortAmount(const QString &amount) {
    const double value = amount.toDouble();
    if (value <= 0) {
        return "0";
    }
    const int decimals = std::clamp(int(std::ceil(-std::log10(value))) + 1, 2, 18);
    return QString::number(value, 'f', decimals);
}

std::optional<CoinDestination> detectCoinDestination(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.contains('\n')) {
        return std::nullopt;
    }

    // Ethereum: 0x… (with a correct checksum if mixed case).
    if (eth::parseAddress(trimmed)) {
        return CoinDestination{&ethereum(), trimmed, {}};
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

QList<FeeLevel> feeLevels(const CoinParams &params) {
    const double blockMinutes = params == litecoin() ? 2.5 : 10.0;
    auto duration = [blockMinutes](int blocks) {
        const int minutes = int(blocks * blockMinutes + 0.5);
        return minutes < 60 ? QString("~%1 min").arg(minutes)
                            : minutes == 60 ? QString("~1 hour") : QString("~%1 hours").arg(minutes / 60);
    };
    return {
        {QString("Fast (%1)").arg(duration(2)), 2},
        {QString("Normal (%1)").arg(duration(6)), 6},
        {QString("Slow (%1)").arg(duration(24)), 24},
    };
}

CoinSendController::CoinSendController(Wallet *wallet, QObject *parent)
    : QObject(parent)
    , m_wallet(wallet)
    , m_vault(CoinVault::forWallet(wallet))
{
}

std::optional<double> CoinSendController::feeRate(const CoinParams &params, int targetBlocks) const {
    CoinWallet *coin = m_vault && m_vault->isUnlocked() ? m_vault->wallet(params) : nullptr;
    if (!coin) {
        return std::nullopt;
    }
    return coin->feeRate(targetBlocks);
}

bool CoinSendController::ensureReady(QWidget *parent, const CoinParams &params) {
    if (!m_vault) {
        return false;
    }
    if (m_vault->hasCoin(params)) {
        return true;
    }
    addCoinToWallet(m_vault, params, parent);
    return false;   // the user sends again once the wallet is synchronized
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
    if (!ensureReady(parent, params)) {
        return;
    }

    CoinWallet *coin = m_vault->wallet(params);
    if (coin->status() != CoinWallet::Status::Synchronized) {
        Utils::showError(parent, "Not synchronized", QString("Wait until %1 is synchronized, then try again.").arg(params.name));
        return;
    }

    const double rate = coin->feeRate(targetBlocks);
    QString error;
    const QStringList onlyCoins = m_vault->coinSelection(params);
    const auto plan = coin->planSend(address, amount, rate, sendAll, &error, onlyCoins);
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
    const QString coinsLine = onlyCoins.isEmpty() ? QString()
        : QString("Coins: %1 selected in the Coins tab\n").arg(plan->inputs.size());
    box.setInformativeText(coinsLine + QString("%1To: %2\nNetwork fee: %3 (%4 sat/vB)\nTotal: %5")
                           .arg(from, to, format(plan->fee))
                           .arg(rate, 0, 'f', 1)
                           .arg(format(plan->amount + plan->fee)));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    QPointer<QWidget> guard(parent);
    const CoinParams *sentParams = &params;
    coin->broadcast(*plan, [this, guard, sentParams](const QString &txid, const QString &error) {
        if (!error.isEmpty()) {
            Utils::showError(guard, "Transaction not sent", error);
            return;
        }
        m_vault->setCoinSelection(*sentParams, {});   // those coins are spent
        Utils::showInfo(guard, "Transaction sent", QString("Transaction ID: %1").arg(txid));
        emit sent(txid);
    });
}

std::optional<QPair<QString, QString>> CoinSendController::ethereumFee(const QString &asset) const {
    const EthWallet *eth = m_vault && m_vault->isUnlocked() ? m_vault->ethereum() : nullptr;
    const auto fees = eth ? eth->fees() : std::nullopt;
    if (!fees) {
        return std::nullopt;
    }
    // A plain transfer uses 21000 gas; a USDT or USDC transfer about 65000.
    const eth::u128 gas = asset == "ETH" ? eth::transferGas : 65000;
    return qMakePair(eth::formatAmount(gas * (fees->baseFee + fees->priorityFee), eth::etherDecimals),
                     eth::formatAmount(gas * fees->maxFeePerGas, eth::etherDecimals));
}

std::optional<double> CoinSendController::ethereumGwei() const {
    const EthWallet *eth = m_vault && m_vault->isUnlocked() ? m_vault->ethereum() : nullptr;
    const auto fees = eth ? eth->fees() : std::nullopt;
    if (!fees) {
        return std::nullopt;
    }
    return double(fees->baseFee + fees->priorityFee) / 1e9;
}

void CoinSendController::sendEthereum(QWidget *parent, const QString &asset, const QString &address,
                                      const QString &amountText) {
    if (!ensureReady(parent, ethereum())) {
        return;
    }
    EthWallet *eth = m_vault->ethereum();
    if (eth->status() != EthWallet::Status::Synchronized) {
        Utils::showError(parent, "Not synchronized", "Wait until Ethereum is synchronized, then try again.");
        return;
    }
    QString text = amountText.trimmed();
    text.replace(',', '.');

    // The nonce and the gas come from the node: a moment.
    QPointer<QWidget> guard(parent);
    QPointer<EthWallet> wallet(eth);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    eth->planSend(asset, address, text, [this, guard, wallet](std::optional<EthWallet::Plan> plan, const QString &error) {
        QApplication::restoreOverrideCursor();
        if (!guard || !wallet) {
            return;
        }
        if (!plan) {
            Utils::showError(guard, "Unable to send", error);
            return;
        }
        const int decimals = EthWallet::decimals(plan->asset);
        const QString amountValue = eth::formatAmount(plan->amount, decimals);
        const QString amount = QString("%1 %2").arg(amountValue, plan->asset);
        const QString maxFeeValue = eth::formatAmount(plan->maxFee, eth::etherDecimals);
        const QString maxFee = QString("%1 ETH%2").arg(maxFeeValue, fiatValue("ETH", maxFeeValue));

        // Explicit confirmation: nothing is signed or sent before this.
        QMessageBox box(guard);
        box.setWindowTitle(QString("Send %1").arg(plan->asset));
        box.setIcon(QMessageBox::Question);
        box.setText(QString("Send %1?").arg(amount + fiatValue(plan->asset, amountValue)));
        // Everything can be selected and copied (e.g. into the converter).
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
        const auto entries = m_vault->wallets(ethereum());
        QString details = entries.size() > 1
                          ? QString("From: Ethereum wallet \"%1\"\n").arg(m_vault->selectedName(ethereum())) : QString();
        details += QString("To: %1\n").arg(eth::checksumAddress(plan->recipient));
        details += plan->asset == "ETH" ? QString("Network: Ethereum\n") : QString("Network: Ethereum (ERC-20)\n");
        details += QString("Network fee: at most %1, usually less").arg(maxFee);
        if (const auto fees = wallet->fees()) {
            const double gwei = double(fees->baseFee + fees->priorityFee) / 1e9;
            details += QString(" (%1 gwei%2)").arg(QString::number(gwei, 'f', gwei < 10 ? 2 : 1),
                                                   gwei >= 6 ? QString(", very busy right now") : QString());
        }
        if (plan->asset == "ETH") {
            const QString total = eth::formatAmount(plan->amount + plan->maxFee, eth::etherDecimals);
            details += QString("\nTotal: at most %1 ETH%2").arg(total, fiatValue("ETH", total));
        }
        box.setInformativeText(details);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes || !wallet) {
            return;
        }
        wallet->broadcast(*plan, [this, guard](const QString &hash, const QString &error) {
            if (!error.isEmpty()) {
                Utils::showError(guard, "Transaction not sent", error);
                return;
            }
            Utils::showInfo(guard, "Transaction sent", QString("Transaction hash: %1").arg(hash));
            emit sent(hash);
        });
    });
}

void CoinSendController::bumpFee(QWidget *parent, const CoinParams &params, CoinWallet *coin, const QString &txid) {
    if (!coin || !ensureReady(parent, params)) {
        return;
    }
    if (coin->status() != CoinWallet::Status::Synchronized) {
        Utils::showError(parent, "Not synchronized", QString("Wait until %1 is synchronized, then try again.").arg(params.name));
        return;
    }
    auto format = [&params](quint64 sats) {
        return QString("%1 %2").arg(biscuit::swap::amount::fromAtomic(sats, params.decimals), params.ticker);
    };
    const double current = coin->transactionFeeRate(txid);
    const double fast = coin->feeRate(2);
    const double minimum = std::ceil(current + 1.0);
    bool ok = false;
    const double rate = QInputDialog::getDouble(parent, "Speed up the transaction",
            QString("Current fee: %1 sat/vB. Fast confirmation now: about %2 sat/vB.\n\nNew fee rate (sat/vB):")
                .arg(current, 0, 'f', 1).arg(fast, 0, 'f', 1),
            std::max(minimum, std::ceil(fast)), minimum, 10000.0, 1, &ok);
    if (!ok) {
        return;
    }
    QString error;
    const auto plan = coin->planBump(txid, rate, &error);
    if (!plan) {
        Utils::showError(parent, "Unable to speed up", error);
        return;
    }

    QMessageBox box(parent);
    box.setWindowTitle("Speed up the transaction");
    box.setIcon(QMessageBox::Question);
    box.setText("Replace the transaction with a higher fee?");
    const auto oldFee = coin->transactionFee(txid);
    QString details = QString("Network fee: %1 → %2 (%3 sat/vB)\nThe payment itself does not change.")
                          .arg(oldFee ? format(*oldFee) : QString("?"), format(plan->fee))
                          .arg(double(plan->fee) / std::max(1, plan->estimatedVsize), 0, 'f', 1);
    if (plan->changeOutput < 0) {
        details += "\nThe change is used up by the fee.";
    }
    box.setInformativeText(details);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    // The answer comes later; this controller may be gone by then.
    QPointer<QWidget> guard(parent);
    QPointer<CoinSendController> self(this);
    coin->broadcast(*plan, [self, guard](const QString &newTxid, const QString &error) {
        if (!error.isEmpty()) {
            Utils::showError(guard, "Transaction not replaced", error);
            return;
        }
        Utils::showInfo(guard, "Transaction replaced", QString("New transaction ID: %1").arg(newTxid));
        if (self) emit self->sent(newTxid);
    });
}

}
