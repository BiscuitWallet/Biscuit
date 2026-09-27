# Biscuit swap core: pure logic (amounts, ranking, trade states, API parsing).
# Depends on Qt Core only, so it can be unit tested without the wallet or the UI.

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(biscuit_swap_core STATIC
        Amount.cpp
        QuoteRanking.cpp
        SwapTypes.cpp
        TrocadorApi.cpp
        SwapHistory.cpp
        AtomicEvents.cpp
        AtomicSwapRecord.cpp
        DemoSwap.cpp
        RequestPolicy.cpp
)
target_include_directories(biscuit_swap_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biscuit_swap_core PUBLIC Qt6::Core)
