// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAPWIDGET_H
#define BISCUIT_SWAPWIDGET_H

#include <QPointer>
#include <QWidget>

#include "swap/SwapManager.h"

class InfoFrame;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class Wallet;

namespace biscuit::coins { class CoinSendController; class CoinWalletBar; }

namespace Ui {
class SwapWidget;
}

// Swap tab: exchanges between the coins Biscuit holds through Trocador. Coins
// are received (and refunded) to this wallet by default, or to any address.
// Popular coins Biscuit does not hold (USDT on Tron, SOL…) and any other coin
// of the partners are sent to, or taken from, a wallet outside Biscuit.
class SwapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SwapWidget(Wallet *wallet, QWidget *parent = nullptr);
    ~SwapWidget() override;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onFromChanged();
    void onToChanged();
    void onReverse();
    void onClear();
    void onGetOffers();
    void onCreateSwap();
    void onShowTradeDetails();
    void refreshTrades();

private:
    biscuit::swap::Asset xmr() const;
    static biscuit::swap::Asset assetOf(const QComboBox *combo);
    // Selects `asset`, adding it first if it came from "Other coin…" on the
    // other side.
    void selectAsset(QComboBox *combo, const biscuit::swap::Asset &asset);
    // "Other coin…": the coin picked in the partners' full list, or the one
    // selected before if cancelled.
    void pickOtherCoin(QComboBox *combo, int previous);
    // What the partners say about a coin (memo, minimum), null if unknown.
    const biscuit::swap::AssetInfo *assetInfo(const biscuit::swap::Asset &asset) const;
    bool sendsXmr() const;
    bool receivesXmr() const;
    bool fixedRate() const;
    void placeAmountField();

    void loadAssets();
    void showAssets(const QList<biscuit::swap::AssetInfo> &assets);
    // Tor mode: exchange swaps are off, atomic swaps remain.
    void updateTorMode();
    void updateForm();
    void clearOffers();
    void onMax();
    void updateWalletRows();
    void updateWalletChoices();
    void updateScanIcons();
    void changeEvent(QEvent *event) override;   // "My … wallet" or another address, Max
    void showOffers();
    void showOffersRows(bool visible);
    void showOffersMenu(const QPoint &pos);
    void setBusy(bool busy, const QString &status = {});
    QString newWalletAddress(const QString &label);
    // Bitcoin or Litecoin address of this wallet for `asset`; sets the BTC/LTC
    // seed up or unlocks it first if needed. Empty if not available (the user
    // was told why, or cancelled).
    QString coinWalletAddress(const biscuit::swap::Asset &asset);
    QString coinWalletLabel(const biscuit::swap::Asset &asset) const;
    // XMR always; BTC or LTC only once added to this wallet (locked counts:
    // the password is asked when needed). Otherwise only other addresses.
    bool walletHolds(const biscuit::swap::Asset &asset) const;
    // Biscuit's ticker for a swap coin: "BTC", "ETH", "USDT"… (empty: Monero
    // or a coin Biscuit does not hold).
    static QString walletTicker(const biscuit::swap::Asset &asset);
    // The same coin on both sides: picks another one in `combo`.
    void moveAway(QComboBox *combo);
    void validateTypedAddresses(const QString &providerId, QList<QPair<biscuit::swap::Asset, QString>> addresses,
                                std::function<void()> done);
    void createTrade(const biscuit::swap::Quote &quote);
    void showTrade(const QString &providerId, const QString &tradeId);
    void sendDeposit(const biscuit::swap::Trade &trade);
    void sendCoinDeposit(const biscuit::swap::Trade &trade);

    QScopedPointer<Ui::SwapWidget> ui;
    QPointer<Wallet> m_wallet;
    biscuit::swap::SwapManager *m_manager;
    biscuit::coins::CoinSendController *m_coins;
    QPushButton *m_scanReceive = nullptr;   // QR code of an address, with the webcam
    QPushButton *m_scanRefund = nullptr;
    QPushButton *m_btnMax = nullptr;
    // Which Bitcoin/Litecoin wallet sends, and which one receives.
    QWidget *m_fromWalletRow = nullptr;
    QWidget *m_toWalletRow = nullptr;
    QList<biscuit::coins::CoinWalletBar *> m_fromBars;   // Bitcoin, Litecoin
    QList<biscuit::coins::CoinWalletBar *> m_toBars;
    // Memo or tag of an address typed by the user (XRP, XLM, TON…).
    QLineEdit *m_receiveMemo = nullptr;
    QLineEdit *m_refundMemo = nullptr;
    QList<biscuit::swap::Quote> m_quotes;
    QList<biscuit::swap::AssetInfo> m_partnerAssets;   // every coin of the partners
    // Selected coins before the last change, for a cancelled "Other coin…".
    int m_lastFrom = -1;
    int m_lastTo = -1;
    bool m_updating = false;
    bool m_assetsLoaded = false;    // the coin list came in
    bool m_loadingAssets = false;   // a request for it is on its way
    QString m_assetsFrom;           // providers the coin list came from (they change with Tor mode)
    InfoFrame *m_torNotice = nullptr;
    QWidget *m_atomicTab = nullptr;
    bool m_mainnet = true;

    // Trade whose deposit is being sent from this wallet.
    QString m_pendingDepositProvider;
    QString m_pendingDepositTrade;
    QString m_pendingDepositDescription;
};

#endif // BISCUIT_SWAPWIDGET_H
