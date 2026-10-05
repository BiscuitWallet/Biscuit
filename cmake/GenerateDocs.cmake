# Help > Documentation: Biscuit's documentation, the same pages as biscuitwallet.com/docs,
# bundled so it works offline. contrib/docs/pages is exported from the website's
# repository (tools/export_app_docs.py).
message(STATUS "Bundling docs")

FILE(GLOB DOCS LIST_DIRECTORIES false "${CMAKE_SOURCE_DIR}/contrib/docs/pages/*")

foreach(FILE ${DOCS})
    cmake_path(GET FILE FILENAME FILE_REL)
    list(APPEND QRC_LIST "        <file alias=\"${FILE_REL}\">${FILE}</file>")
endforeach()

list(JOIN QRC_LIST "\n" QRC_DATA)
configure_file("cmake/assets_docs.qrc" "${CMAKE_CURRENT_SOURCE_DIR}/src/assets_docs.qrc")
