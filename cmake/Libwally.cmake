# Biscuit: libwally-core (keys, BIP39/BIP32, addresses, signatures for BTC/LTC).
#
# libwally ships its CMake build as _CMakeLists.txt / _cmake (autotools is the
# default). To keep the submodule untouched, it is copied to the build tree and
# the files are renamed there.

set(LIBWALLY_SUBMODULE "${CMAKE_SOURCE_DIR}/external/libwally-core")
set(LIBWALLY_SRC "${CMAKE_BINARY_DIR}/libwally-src")

if (NOT EXISTS "${LIBWALLY_SUBMODULE}/include/wally_core.h")
    message(FATAL_ERROR "libwally-core is missing, run: git submodule update --init --recursive")
endif()

file(REMOVE_RECURSE "${LIBWALLY_SRC}")
file(COPY "${LIBWALLY_SUBMODULE}/" DESTINATION "${LIBWALLY_SRC}" PATTERN ".git" EXCLUDE)
file(RENAME "${LIBWALLY_SRC}/_CMakeLists.txt" "${LIBWALLY_SRC}/CMakeLists.txt")
file(RENAME "${LIBWALLY_SRC}/_cmake" "${LIBWALLY_SRC}/cmake")
file(RENAME "${LIBWALLY_SRC}/src/_CMakeLists.txt" "${LIBWALLY_SRC}/src/CMakeLists.txt")

# The library build globs ccan's test harness (tap.c) in with the sources; it
# is unused and does not compile with GCC 15 (vasprintf without _GNU_SOURCE).
file(READ "${LIBWALLY_SRC}/src/CMakeLists.txt" LIBWALLY_SRC_CMAKE)
string(REPLACE "\"ccan/ccan/tap/*.[ch]\"" "" LIBWALLY_SRC_CMAKE "${LIBWALLY_SRC_CMAKE}")
file(WRITE "${LIBWALLY_SRC}/src/CMakeLists.txt" "${LIBWALLY_SRC_CMAKE}")

# Its config.h declares ssize_t for MSVC on Windows; MinGW already has it.
file(READ "${LIBWALLY_SRC}/cmake/config.h.in" LIBWALLY_CONFIG_H)
string(REPLACE "#if defined (_WIN32) && !defined(_SSIZE_T_DECLARED)"
               "#if defined (_WIN32) && !defined(__MINGW32__) && !defined(_SSIZE_T_DECLARED)"
               LIBWALLY_CONFIG_H "${LIBWALLY_CONFIG_H}")
file(WRITE "${LIBWALLY_SRC}/cmake/config.h.in" "${LIBWALLY_CONFIG_H}")

# Release builds (depends / Guix) cross-compile with a toolchain file: build
# libwally with the same one, so it gets the same compiler, sysroot and flags.
set(LIBWALLY_TOOLCHAIN_ARGS "")
if (CMAKE_TOOLCHAIN_FILE)
    set(LIBWALLY_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()

# libwally's CMake expects to be the top-level project, so it is built on its
# own (ExternalProject) and its static libraries are imported.
include(ExternalProject)
set(LIBWALLY_BUILD "${CMAKE_BINARY_DIR}/libwally-build")
set(LIBWALLY_LIB "${LIBWALLY_BUILD}/src/${CMAKE_STATIC_LIBRARY_PREFIX}wallycore${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(LIBWALLY_SECP_LIB "${LIBWALLY_BUILD}/src/secp256k1/lib/${CMAKE_STATIC_LIBRARY_PREFIX}secp256k1${CMAKE_STATIC_LIBRARY_SUFFIX}")

ExternalProject_Add(libwally_build
        SOURCE_DIR "${LIBWALLY_SRC}"
        BINARY_DIR "${LIBWALLY_BUILD}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DWALLYCORE_BUILD_ELEMENTS=OFF   # no Liquid, stay light
            -DWALLYCORE_ENABLE_TESTS=OFF
            -DWALLYCORE_INSTALL=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            ${LIBWALLY_TOOLCHAIN_ARGS}
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${LIBWALLY_LIB}" "${LIBWALLY_SECP_LIB}"
)

add_library(wallycore STATIC IMPORTED GLOBAL)
set_target_properties(wallycore PROPERTIES
        IMPORTED_LOCATION "${LIBWALLY_LIB}"
        # secp256k1's public headers too (silent payments use its scalar and
        # point operations that libwally does not wrap).
        INTERFACE_INCLUDE_DIRECTORIES "${LIBWALLY_SRC}/include;${LIBWALLY_SRC}/src/secp256k1/include"
        INTERFACE_LINK_LIBRARIES "${LIBWALLY_SECP_LIB}"
)
add_dependencies(wallycore libwally_build)
