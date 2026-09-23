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
