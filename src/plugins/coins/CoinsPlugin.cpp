// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinsPlugin.h"

#include "plugins/PluginRegistry.h"

namespace {
    Plugin *createBitcoin() {
        return new CoinPlugin("bitcoin", "Bitcoin", "bitcoin.png", 35, biscuit::coins::bitcoin());
    }
    Plugin *createLitecoin() {
        return new CoinPlugin("litecoin", "Litecoin", "litecoin.png", 36, biscuit::coins::litecoin());
    }

    const bool registered = [] {
        PluginRegistry::registerPlugin(createBitcoin());
        PluginRegistry::getInstance().registerPluginCreator(&createBitcoin);
        PluginRegistry::registerPlugin(createLitecoin());
        PluginRegistry::getInstance().registerPluginCreator(&createLitecoin);
        return true;
    }();
}
