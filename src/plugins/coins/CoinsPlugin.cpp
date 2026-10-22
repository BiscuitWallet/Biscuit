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
    Plugin *createDogecoin() {
        return new CoinPlugin("dogecoin", "Dogecoin", "dogecoin.png", 37, biscuit::coins::dogecoin());
    }

    const bool registered = [] {
        PluginRegistry::registerPlugin(createBitcoin());
        PluginRegistry::getInstance().registerPluginCreator(&createBitcoin);
        PluginRegistry::registerPlugin(createLitecoin());
        PluginRegistry::getInstance().registerPluginCreator(&createLitecoin);
        PluginRegistry::registerPlugin(createDogecoin());
        PluginRegistry::getInstance().registerPluginCreator(&createDogecoin);
        return true;
    }();
}
