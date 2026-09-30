// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: 2011 Felix Geyer <debfx@fobos.de>
// SPDX-FileCopyrightText: 2020 KeePassXC Team <team@keepassxc.org>
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_CONFIG_H
#define FEATHER_CONFIG_H

#include <QObject>
#include <QSettings>
#include <QPointer>
#include <QDir>

class Config : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(Config)

    enum ConfigKey
    {
        // General
        firstRun,
        warnOnStagenet,
        warnOnTestnet,
        systemTorNotice,
        appearance,
        homeRecentActivity,
        warnOnKiImport,

        homeWidget,
        donateBeg,
        showHistorySyncNotice,

        geometry,
        windowState,
        GUI_HistoryViewState,
        geometryOTSWizard,

        // Wallets
        walletDirectory, // Directory where wallet files are stored
        autoOpenWalletPath,
        recentlyOpenedWallets,

        // Nodes
        nodes,
        nodeSource,
        useOnionNodes,

        // Tabs
        enabledTabs,
        showSearchbar,

        // History
        historyShowFullTxid,

        // Receive
        showUsedAddresses,
        showHiddenAddresses,
        showFullAddresses,
        showChangeAddresses,
        showAddressIndex,
        showAddressLabels,

        // Settings
        lastSettingsPage,

        // Appearance
        skin,
        amountPrecision,
        dateFormat,
        timeFormat,
        balanceDisplay,
        balanceShowFiat,
        preferredFiatCurrency,

        // Network -> Proxy
        proxy,
        socks5Host,
        socks5Port,
        socks5User,
        socks5Pass,
        useLocalTor, // Prevents Feather from starting bundled Tor daemon
        torOnlyAllowOnion,
        torPrivacyLevel, // Tor node network traffic strategy
        torManagedPort, // Port for managed Tor daemon
        initSyncThreshold, // Switch to Tor after initial sync threshold blocks

        // Network -> Websocket
        disableWebsocket,
        dataTorOnly,        // Biscuit: third-party data through Tor even outside Tor mode
        electrumServerBTC,  // Biscuit: own Electrum server ("host:port", tcp:// for .onion or local), empty = public servers
        electrumServerLTC,

        // Network -> Offline
        offlineMode,

        // Storage -> Logging
        writeStackTraceToDisk,
        disableLogging,
        logLevel,

        // Storage -> Misc
        writeRecentlyOpenedWallets,

        // Display
        hideBalance,
        hideUpdateNotifications,
        hideNotifications,
        warnOnExternalLink,
        inactivityLockEnabled,
        inactivityLockTimeout,
        lockOnMinimize,
        showTrayIcon,
        minimizeToTray,

        // Transactions
        multiBroadcast,
        offlineTxSigningMethod,
        offlineTxSigningForceKISync,
        manualFeeTierSelection,
        subtractFeeFromAmount,

        // Misc
        blockExplorers,
        blockExplorer,
        lastPath,
        
        // UR
        URmsPerFragment,
        URfragmentLength,
        URfountainCode,

        // Camera
        cameraManualExposure,
        cameraExposureTime,

        fiatSymbols,
        cryptoSymbols,

        enabledPlugins,
        restartRequired,

        // Tickers
        tickers,
        tickersShowFiatBalance,

        // Biscuit: swaps
        swapDisabledProviders,
        atomicSwapTor,       // atomic swaps through Tor when Tor mode is off
    };

    enum PrivacyLevel {
        allTorExceptNode = 0,
        allTorExceptInitSync,
        allTor
    };

    enum BalanceDisplay {
        totalBalance = 0,
        spendablePlusUnconfirmed,
        spendable
    };

    enum Proxy {
        None = 0,
        Tor,
        i2p,
        socks5
    };

    enum OTSMethod {
        UnifiedResources = 0,
        FileTransfer
    };
    
    ~Config() override;
    QVariant get(ConfigKey key);
    QString getFileName();
    void set(ConfigKey key, const QVariant& value);
    void remove(ConfigKey key);
    void sync();
    void resetToDefaults();

    static QDir defaultConfigDir();

    static Config* instance();

signals:
    void changed(Config::ConfigKey key);

private:
    Config(const QString& fileName, QObject* parent = nullptr);
    explicit Config(QObject* parent);
    void init(const QString& configFileName);

    static QPointer<Config> m_instance;

    QScopedPointer<QSettings> m_settings;
    QHash<QString, QVariant> m_defaults;
};

inline Config* conf()
{
    return Config::instance();
}

#endif //FEATHER_CONFIG_H
