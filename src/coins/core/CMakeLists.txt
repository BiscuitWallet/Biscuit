# Biscuit BTC/LTC wallet core: keys, addresses, transactions (libwally-core + Qt Core).
# No network, no UI: fully unit tested.

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(biscuit_coins_core STATIC
        Addresses.cpp
        Bip39.cpp
        CoinParams.cpp
        HdAccount.cpp
        WallyInit.cpp
)
target_include_directories(biscuit_coins_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biscuit_coins_core PUBLIC Qt6::Core PRIVATE wallycore)
