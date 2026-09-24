# Biscuit BTC/LTC wallet core: keys, addresses, transactions (libwally-core + Qt Core).
# No network, no UI: fully unit tested.

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(biscuit_coins_core STATIC
        Addresses.cpp
        Bip39.cpp
        CoinParams.cpp
        HdAccount.cpp
        WallyInit.cpp
        WalletFile.cpp
        Transactions.cpp
        Electrum.cpp
        ElectrumServers.cpp
)
target_include_directories(biscuit_coins_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# libsodium (already used by Monero) for the wallet file encryption.
find_library(SODIUM_LIBRARY sodium)
find_path(SODIUM_INCLUDE_DIR sodium.h)
target_include_directories(biscuit_coins_core PRIVATE ${SODIUM_INCLUDE_DIR})
target_link_libraries(biscuit_coins_core PUBLIC Qt6::Core PRIVATE wallycore ${SODIUM_LIBRARY})
