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
foreach(_test test_amount test_quote_ranking test_trade_status test_trocador_api)
    biscuit_add_test(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE biscuit_swap_core)
endforeach()
