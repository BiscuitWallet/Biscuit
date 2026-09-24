# Biscuit public data feed: pure conversion of public API answers (Qt Core only).

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(biscuit_datafeed_core STATIC PublicData.cpp)
target_include_directories(biscuit_datafeed_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biscuit_datafeed_core PUBLIC Qt6::Core)
