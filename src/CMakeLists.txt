set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
IF(STATIC AND APPLE)
    set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
ENDIF()

set(QT_COMPONENTS
        Core
        Widgets
        Gui
        Network
        Svg
        WebSockets
        SvgWidgets
)

if (WITH_SCANNER)
    list(APPEND QT_COMPONENTS
            Multimedia
            MultimediaWidgets)
endif()

IF (UNIX AND NOT APPLE)
    list(APPEND QT_COMPONENTS WaylandClient)
endif()

find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

if (CHECK_UPDATES)
    add_subdirectory(openpgp)
endif()

qt_add_resources(RESOURCES assets.qrc assets_tor.qrc assets_docs.qrc)

# Compile source files (.h/.cpp)
file(GLOB SOURCE_FILES
        "*.h"
        "*.cpp"
        "api/*.h"
        "api/*.cpp"
        "utils/*.h"
        "utils/*.cpp"
        "utils/os/*.h"
        "utils/os/*.cpp"
        "libwalletqt/*.h"
        "libwalletqt/*.cpp"
        "libwalletqt/rows/*.h"
        "libwalletqt/rows/*.cpp"
        "daemon/*.h"
        "daemon/*.cpp"
        "model/*.h"
        "model/*.cpp"
        "qt/*.h"
        "qt/*.cpp"
        "ui/qdarkstyle/style.qrc"
        "ui/BreezeStyleSheets/breeze.qrc"
        "widgets/*.h"
        "widgets/*.cpp"
        "wizard/*.h"
        "wizard/*.cpp"
        "wallet/*.h"
        "wallet/*.cpp"
        "qrcode/*.h"
        "qrcode/*.cpp"
        "dialog/*.h"
        "dialog/*.cpp"
        "polyseed/*.h"
        "polyseed/*.cpp"
        "polyseed/*.c"
        "monero_seed/argon2/blake2/*.c"
        "monero_seed/argon2/*.c"
        "monero_seed/*.cpp"
        "monero_seed/*.c"
        "monero_seed/*.hpp"
        "plugins/*.cpp"
        "plugins/*.h"
        "swap/*.h"
        "swap/*.cpp"
        "datafeed/*.h"
        "datafeed/*.cpp"
        "coins/*.h"
        "coins/*.cpp"
        )

get_cmake_property(_vars VARIABLES)
set(PLUGIN_PREFIX "WITH_PLUGIN_")

foreach (_var ${_vars})
    string(REGEX MATCH "^${PLUGIN_PREFIX}" _isPlugin ${_var})

    if (NOT ${_var})
        continue()
    endif()

    if(_isPlugin)
        string(REPLACE "${PLUGIN_PREFIX}" "" _suffix ${_var})
        string(TOLOWER "${_suffix}" _plugin)
        message(STATUS "Adding plugin: ${_plugin}")
        file (GLOB PLUGIN_FILES
                "plugins/${_plugin}/*.cpp"
                "plugins/${_plugin}/*.h"
        )
        list (APPEND SOURCE_FILES
                ${PLUGIN_FILES}
        )
    endif()
endforeach()

if (CHECK_UPDATES)
    file(GLOB UPDATER_FILES
            "utils/updater/*.h"
            "utils/updater/*.cpp")
endif()

if (WITH_SCANNER)
    file(GLOB QRCODE_UTILS_FILES
            "qrcode/utils/*.h"
            "qrcode/utils/*.cpp"
            "wizard/offline_tx_signing/*.h"
            "wizard/offline_tx_signing/*.cpp")
endif()

if (WITH_SCANNER)
    file(GLOB SCANNER_FILES
            "qrcode/scanner/*.h"
            "qrcode/scanner/*.cpp")
endif()

list(APPEND SOURCE_FILES
        ${UPDATER_FILES}
        ${QRCODE_UTILS_FILES}
        ${SCANNER_FILES})

set(EXECUTABLE_FLAG)
if(MINGW)
    set(EXECUTABLE_FLAG WIN32)

    set(ICON ${PROJECT_SOURCE_DIR}/src/assets/images/appicons/appicon.ico)
    set(ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    set(ICON_RES ${CMAKE_CURRENT_BINARY_DIR}/icon.o)
    file(WRITE ${ICON_RC} "IDI_ICON1 ICON DISCARDABLE \"${ICON}\"")
    find_program(Qt_WINDRES_EXECUTABLE NAMES windres x86_64-w64-mingw32-windres REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)
    add_custom_command(OUTPUT ${ICON_RES} COMMAND ${Qt_WINDRES_EXECUTABLE} ${ICON_RC} ${ICON_RES} MAIN_DEPENDENCY ${ICON_RC})
    list(APPEND RESOURCES ${ICON_RES})
endif()

add_executable(feather ${EXECUTABLE_FLAG} main.cpp
        ${SOURCE_FILES}
        ${RESOURCES}
        ${ASSETS_TOR}
)

set_target_properties(feather PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        OUTPUT_NAME "biscuit"
        LINK_FLAGS_RELEASE -s
)

if(STACK_TRACE)
    message(STATUS "Stack Trace Enabled")
    if (STATIC)
        set_property(TARGET feather APPEND PROPERTY LINK_FLAGS "-Wl,--wrap=__cxa_throw")
    endif()
endif()

target_include_directories(feather PUBLIC
        ${CMAKE_BINARY_DIR}/src/feather_autogen/include
        ${CMAKE_SOURCE_DIR}/monero/include
        ${CMAKE_SOURCE_DIR}/monero/src
        ${CMAKE_SOURCE_DIR}/monero/external
        ${CMAKE_SOURCE_DIR}/monero/external/easylogging++
        ${CMAKE_SOURCE_DIR}/monero/contrib/epee/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libwalletqt
        ${CMAKE_CURRENT_SOURCE_DIR}/model
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/qrcode
        ${Boost_INCLUDE_DIRS}
        ${QtCore_INCLUDE_DIRS}
        ${QtWidgets_INCLUDE_DIRS}
        ${QtGui_INCLUDE_DIRS}
        ${QtNetwork_INCLUDE_DIRS}
        ${QtSvg_INCLUDE_DIRS}
        ${QtSvgWidgets_INCLUDE_DIRS}
        ${QtXml_INCLUDE_DIRS}
        ${QtWebSockets_INCLUDE_DIRS}
        ${LIBZIP_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${POLYSEED_INCLUDE_DIR}
        ${BCUR_INCLUDE_DIR}
)

target_compile_definitions(feather PRIVATE FEATHER_VERSION="${PROJECT_VERSION}")
target_compile_definitions(feather PRIVATE FEATHER_TARGET_TRIPLET="${FEATHER_TARGET_TRIPLET}")
target_compile_definitions(feather PRIVATE TOR_VERSION="${TOR_VERSION}")

if(WITH_FEATHER_SERVICES)
    target_compile_definitions(feather PRIVATE WITH_FEATHER_SERVICES=1)
endif()

# Trocador. Official builds go through the Biscuit relay, which holds the partner
# key: set(BISCUIT_TROCADOR_RELAY_URL "https://relay.example.org/api/") in
# secrets.cmake (ignored by git) or with -D. The key is then never compiled in.
# Development builds may instead call Trocador directly with
# set(BISCUIT_TROCADOR_API_KEY "..."). With neither, swaps run in demo mode.
if(EXISTS "${CMAKE_SOURCE_DIR}/secrets.cmake")
    include("${CMAKE_SOURCE_DIR}/secrets.cmake")
endif()
if(BISCUIT_TROCADOR_RELAY_URL)
    if(NOT BISCUIT_TROCADOR_RELAY_URL MATCHES "^(https://[^/]+|http://(127\\.0\\.0\\.1|localhost)(:[0-9]+)?)/api/$")
        message(FATAL_ERROR "BISCUIT_TROCADOR_RELAY_URL must be https://<host>/api/ (or http://127.0.0.1:<port>/api/ for tests)")
    endif()
    target_compile_definitions(feather PRIVATE BISCUIT_TROCADOR_RELAY_URL="${BISCUIT_TROCADOR_RELAY_URL}")
    if(BISCUIT_TROCADOR_API_KEY)
        message(WARNING "BISCUIT_TROCADOR_API_KEY ignored: the relay holds the key, it is not compiled in")
    endif()
    message(STATUS "Trocador: through the relay ${BISCUIT_TROCADOR_RELAY_URL}")
elseif(BISCUIT_TROCADOR_API_KEY)
    # Masked into a generated header (build tree only), never a compiler flag.
    include(BiscuitSecrets)
    biscuit_write_secret_header(trocador_key "${BISCUIT_TROCADOR_API_KEY}" "${CMAKE_BINARY_DIR}/generated/BiscuitSecrets.h")
    target_include_directories(feather PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(feather PRIVATE BISCUIT_HAS_TROCADOR_KEY=1)
    message(STATUS "Trocador: direct with a key (development build only)")
else()
    message(STATUS "Trocador: no relay and no key (swaps run in demo mode)")
endif()

# Atomic swaps: the biscuit-swapd helper (external/biscuit-swapd, see BUILD.md)
# is copied next to the Biscuit executable when BISCUIT_SWAPD_BINARY points to
# it, or when a release build of it is found in the submodule. Without it, the
# atomic swap tab says the helper is missing.
set(BISCUIT_SWAPD_DEFAULT "${CMAKE_SOURCE_DIR}/external/biscuit-swapd/target/release/biscuit-swapd${CMAKE_EXECUTABLE_SUFFIX}")
if(NOT BISCUIT_SWAPD_BINARY AND EXISTS "${BISCUIT_SWAPD_DEFAULT}")
    set(BISCUIT_SWAPD_BINARY "${BISCUIT_SWAPD_DEFAULT}")
endif()
if(BISCUIT_SWAPD_BINARY)
    add_custom_command(TARGET feather POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BISCUIT_SWAPD_BINARY}" "$<TARGET_FILE_DIR:feather>/biscuit-swapd${CMAKE_EXECUTABLE_SUFFIX}"
            COMMENT "Bundling biscuit-swapd")
    if(APPLE)
        # Already inside Biscuit.app (Contents/MacOS), installed with the bundle.
    elseif(DEPENDS)
        install(PROGRAMS "${BISCUIT_SWAPD_BINARY}" DESTINATION ${CMAKE_INSTALL_PREFIX}
                RENAME "biscuit-swapd${CMAKE_EXECUTABLE_SUFFIX}")
    else()
        install(PROGRAMS "${BISCUIT_SWAPD_BINARY}" DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
                RENAME "biscuit-swapd${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
    message(STATUS "Atomic swaps: bundling ${BISCUIT_SWAPD_BINARY}")
endif()

if(WITH_SCANNER)
    target_include_directories(feather PUBLIC
            ${QtMultimedia_INCLUDE_DIRS}
            ${QtMultimediaWidgets_INCLUDE_DIRS}
            ${ZXING_INCLUDE_DIRS}
    )
endif()

if(SELF_CONTAINED)
    target_compile_definitions(feather PRIVATE SELF_CONTAINED=1)
endif()

if (CHECK_UPDATES)
    target_compile_definitions(feather PRIVATE CHECK_UPDATES=1)
endif()

if(TOR_DIR)
    target_compile_definitions(feather PRIVATE HAS_TOR_BIN=1)
endif()

if(WITH_SCANNER)
    target_compile_definitions(feather PRIVATE WITH_SCANNER=1)
endif()

# TODO: PLACEHOLDER
target_compile_definitions(feather PRIVATE HAS_WEBSOCKET=1)

if(PLATFORM_INSTALLER)
    target_compile_definitions(feather PRIVATE PLATFORM_INSTALLER=1)
endif()

if(TOR_INSTALLED)
    target_compile_definitions(feather PRIVATE TOR_INSTALLED=1)
endif()

if(STACK_TRACE)
    target_compile_definitions(feather PRIVATE STACK_TRACE=1)
endif()

if(STATIC)
    target_compile_definitions(feather PRIVATE STATIC=1)
endif()

if(OFFICIAL_BUILD)
    target_compile_definitions(feather PRIVATE OFFICIAL_BUILD=1)
endif()

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(feather PRIVATE QT_NO_DEBUG=1)
endif()

target_compile_definitions(feather
        PUBLIC
        ${QtCore_DEFINITIONS}
        ${QtWidgets_DEFINITIONS}
        ${QtGui_DEFINITIONS}
        ${QtNetwork_DEFINITIONS}
        ${QtSvg_DEFINITIONS}
        ${QtSvgWidgets_DEFINITIONS}
        ${QtXml_DEFINITIONS}
        ${QtWebSockets_DEFINITIONS}
)

if (WITH_SCANNER)
    target_compile_definitions(feather PUBLIC
            ${QtMultimedia_DEFINITIONS}
            ${QtMultimediaWidgets_DEFINITIONS}
    )
endif()

if (DEPENDS)
    target_link_directories(feather PRIVATE "${LIB_DIR}")
endif()

target_link_libraries(feather PRIVATE
        biscuit_swap_core
        biscuit_datafeed_core
        biscuit_coins_core
        wallet_api
        epee
        easylogging
        ringct
        version
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${EXTRA_LIBRARIES}
        Qt::Core
        Qt::Widgets
        Qt::Gui
        Qt::Network
        Qt::Svg
        Qt::WebSockets
        Qt::SvgWidgets
        Threads::Threads
        ${QRENCODE_LIBRARY}
        ${POLYSEED_LIBRARY}
        ${ICU_LIBRARIES}
        ${LIBZIP_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${BCUR_LIBRARY}
)

if(CHECK_UPDATES)
    target_link_libraries(feather PRIVATE openpgp)
endif()

if(DEVICE_TREZOR_READY)
    find_package(Protobuf CONFIG)
    if (NOT Protobuf_FOUND)
        find_package(Protobuf REQUIRED)
    endif()
    target_link_libraries(feather PRIVATE ${TREZOR_DEP_LIBS})
endif()

if (WITH_SCANNER)
    target_link_libraries(feather PRIVATE
            Qt::Multimedia
            Qt::MultimediaWidgets
            ${ZXING_LIBRARIES}
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(feather PRIVATE Qt::WaylandClient)
endif()

if(STATIC AND APPLE)
    target_link_libraries(feather PRIVATE Qt6::QDarwinCameraPermissionPlugin)
endif()

if(STATIC AND UNIX AND NOT APPLE)
    target_link_libraries(feather PRIVATE Qt6::QComposePlatformInputContextPlugin)
    target_link_libraries(feather PRIVATE Qt6::QWaylandIntegrationPlugin)
endif()

if(DEPENDS AND UNIX AND NOT APPLE)
    # workaround for: undefined reference to `XauGetBestAuthByAddr'
    find_library(LIBXAU_LIBRARY Xau REQUIRED)
    message(STATUS "libXau: libraries at ${LIBXAU_LIBRARY}")
    target_link_libraries(XCB::XCB INTERFACE ${LIBXAU_LIBRARY})

    # make sure Wayland::Client links to libffi
    find_library(LIBFFI_LIBRARY ffi REQUIRED)
    message(STATUS "libffi: libraries at ${LIBFFI_LIBRARY}")
    target_link_libraries(Wayland::Client INTERFACE ${LIBFFI_LIBRARY})
endif()

if(STACK_TRACE AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(feather PRIVATE -rdynamic)
endif()

if(DEPENDS)
    install(TARGETS feather
            DESTINATION ${CMAKE_INSTALL_PREFIX}
    )
else()
    install(TARGETS feather
            DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    )

    if (UNIX AND NOT APPLE)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/biscuit.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/32x32.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/32x32/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/48x48.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/48x48/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/64x64.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/64x64/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/96x96.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/96x96/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/128x128.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/128x128/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/256x256.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps RENAME "biscuit.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/512x512.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps RENAME "biscuit.png")
    endif()
endif()

if(APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist.in ${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist @ONLY)

    set_target_properties(feather PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist"
            LINK_FLAGS_RELEASE -s
    )

    file(COPY "${CMAKE_SOURCE_DIR}/src/assets/images/appicons/appicon.icns" DESTINATION "${CMAKE_SOURCE_DIR}/installed/biscuit.app/Contents/Resources/" )

    # Biscuit: also put the icon in the bundle built in build/bin (Feather only did it
    # for the packaging copy in installed/), otherwise macOS shows a generic icon.
    set(BISCUIT_ICNS "${CMAKE_SOURCE_DIR}/src/assets/images/appicons/appicon.icns")
    set_source_files_properties(${BISCUIT_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(feather PRIVATE ${BISCUIT_ICNS})
endif()

if(APPLE AND BISCUIT_BUNDLE_TOR AND BISCUIT_TOR_BIN)
    target_compile_definitions(feather PRIVATE HAS_TOR_BIN=1)
    add_custom_command(TARGET feather POST_BUILD
            COMMAND "${CMAKE_SOURCE_DIR}/contrib/macdeploy/bundle-tor.sh" "${BISCUIT_TOR_BIN}" "$<TARGET_BUNDLE_DIR:feather>"
            VERBATIM)
endif()

# Biscuit: sign the bundle in build/bin once everything is in it (ad hoc, no
# Apple account). Bundling Tor and the helper breaks the linker signature, and
# macOS then takes the running app for another one than the Dock icon.
if(APPLE)
    add_custom_command(TARGET feather POST_BUILD
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:feather>"
            COMMENT "Signing the app bundle (ad hoc)"
            VERBATIM)
endif()

qt_finalize_executable(feather)
