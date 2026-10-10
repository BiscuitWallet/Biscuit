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
        SilentPayments.cpp
        MessageSigning.cpp
        Electrum.cpp
        ElectrumServers.cpp
        Eth.cpp
        EthApi.cpp
        Keccak.c
)
target_include_directories(biscuit_coins_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Monero's Keccak-256 (Keccak.c), and the headers it includes.
target_include_directories(biscuit_coins_core PRIVATE
        ${CMAKE_SOURCE_DIR}/monero/src
        ${CMAKE_SOURCE_DIR}/monero/contrib/epee/include
        ${Boost_INCLUDE_DIRS})
# libsodium (already used by Monero) for the wallet file encryption.
find_library(SODIUM_LIBRARY sodium)
find_path(SODIUM_INCLUDE_DIR sodium.h)
target_include_directories(biscuit_coins_core PRIVATE ${SODIUM_INCLUDE_DIR})
target_link_libraries(biscuit_coins_core PUBLIC Qt6::Core PRIVATE wallycore ${SODIUM_LIBRARY})
