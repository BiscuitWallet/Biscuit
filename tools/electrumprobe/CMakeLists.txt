cmake_minimum_required(VERSION 3.18)
project(electrumprobe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
# Reuse the main build's libwally copy and coins core.
set(CMAKE_SOURCE_DIR_BISCUIT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(electrumprobe main.cpp
        ${CMAKE_SOURCE_DIR_BISCUIT}/src/coins/ElectrumClient.cpp
        ${CMAKE_SOURCE_DIR_BISCUIT}/src/coins/ElectrumClient.h
        ${CMAKE_SOURCE_DIR_BISCUIT}/src/coins/core/Electrum.cpp)
target_include_directories(electrumprobe PRIVATE
        ${CMAKE_SOURCE_DIR_BISCUIT}/src/coins
        ${CMAKE_SOURCE_DIR_BISCUIT}/src/coins/core
        ${BISCUIT_BUILD}/libwally-src/include)
target_link_libraries(electrumprobe PRIVATE Qt6::Core Qt6::Network
        ${BISCUIT_BUILD}/src/coins/core/libbiscuit_coins_core.a
        ${BISCUIT_BUILD}/libwally-build/src/libwallycore.a
        ${BISCUIT_BUILD}/libwally-build/src/secp256k1/lib/libsecp256k1.a
        /opt/homebrew/lib/libsodium.dylib)
