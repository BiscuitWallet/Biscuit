# Biscuit unit tests (Qt Test + CTest)
# Build with -DBUILD_TESTS=ON, run with `ctest --output-on-failure`.

find_package(Qt6 REQUIRED COMPONENTS Core Test)

function(biscuit_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

biscuit_add_test(test_semanticversion test_semanticversion.cpp)

# Swap core
foreach(_test test_amount test_quote_ranking test_trade_status test_trocador_api test_swap_history test_demo_swap test_request_policy test_atomic_events)
    biscuit_add_test(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE biscuit_swap_core)
endforeach()

# Public data feed
biscuit_add_test(test_public_data test_public_data.cpp)
target_link_libraries(test_public_data PRIVATE biscuit_datafeed_core)

# BTC/LTC wallets
biscuit_add_test(test_coins_keys test_coins_keys.cpp)
target_link_libraries(test_coins_keys PRIVATE biscuit_coins_core)
biscuit_add_test(test_wallet_file test_wallet_file.cpp)
target_link_libraries(test_wallet_file PRIVATE biscuit_coins_core)
biscuit_add_test(test_coins_tx test_coins_tx.cpp)
target_link_libraries(test_coins_tx PRIVATE biscuit_coins_core)
biscuit_add_test(test_eth test_eth.cpp)
target_link_libraries(test_eth PRIVATE biscuit_coins_core)
biscuit_add_test(test_eth_api test_eth_api.cpp)
target_link_libraries(test_eth_api PRIVATE biscuit_coins_core)
biscuit_add_test(test_electrum test_electrum.cpp)
target_link_libraries(test_electrum PRIVATE biscuit_coins_core)
biscuit_add_test(test_electrum_server test_electrum_server.cpp ${CMAKE_SOURCE_DIR}/src/coins/ElectrumClient.cpp)
find_package(Qt6 REQUIRED COMPONENTS Network)
target_include_directories(test_electrum_server PRIVATE ${CMAKE_SOURCE_DIR}/src/coins)
target_link_libraries(test_electrum_server PRIVATE biscuit_coins_core Qt6::Network)
biscuit_add_test(test_message_signing test_message_signing.cpp)
target_link_libraries(test_message_signing PRIVATE biscuit_coins_core wallycore)
biscuit_add_test(test_silent_payments test_silent_payments.cpp)
target_link_libraries(test_silent_payments PRIVATE biscuit_coins_core wallycore)
target_compile_definitions(test_silent_payments PRIVATE BISCUIT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# The release key and the updater's OpenPGP code (only built with CHECK_UPDATES).
if(TARGET openpgp)
    biscuit_add_test(test_release_signature test_release_signature.cpp)
    target_link_libraries(test_release_signature PRIVATE openpgp)
    target_include_directories(test_release_signature PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_release_signature PRIVATE
            BISCUIT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
            BISCUIT_KEY_FILE="${CMAKE_SOURCE_DIR}/src/assets/gpg_keys/biscuit.asc")
endif()
