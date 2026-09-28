cmake_minimum_required(VERSION 3.20)

project(iTool VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

# Keep the semantic version in project(... VERSION ...). The remaining build
# identity is collected automatically whenever CMake configures the project.
string(TIMESTAMP ITOOL_BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP ITOOL_BUILD_TIME "%H:%M:%S")
execute_process(
    COMMAND git describe --tags --always --dirty=-dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE ITOOL_GIT_RESULT
    OUTPUT_VARIABLE ITOOL_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT ITOOL_GIT_RESULT EQUAL 0 OR ITOOL_GIT_DESCRIBE STREQUAL "")
    set(ITOOL_GIT_DESCRIBE "v${PROJECT_VERSION}")
endif()
execute_process(
    COMMAND git rev-parse --short=8 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ITOOL_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(ITOOL_GIT_HASH STREQUAL "")
    set(ITOOL_GIT_HASH "unknown")
endif()
set(ITOOL_VERSION_STRING "${ITOOL_GIT_DESCRIBE}")

# Numeric platform metadata cannot contain commit counts, hashes or "dirty".
# Take it from the nearest vMAJOR.MINOR.PATCH tag when present, otherwise use
# project(... VERSION ...), which is also the pre-tag development fallback.
set(ITOOL_PLATFORM_VERSION "${PROJECT_VERSION}")
set(ITOOL_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(ITOOL_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(ITOOL_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
if(ITOOL_GIT_DESCRIBE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-|$)")
    set(ITOOL_PLATFORM_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(ITOOL_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(ITOOL_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(ITOOL_VERSION_PATCH "${CMAKE_MATCH_3}")
endif()
string(REGEX REPLACE "[^A-Za-z0-9._+-]" "_" ITOOL_FILENAME_VERSION
       "${ITOOL_VERSION_STRING}")
set(ITOOL_ARTIFACT_SUFFIX "" CACHE STRING
    "Optional suffix for side-by-side test artifacts")
set(ITOOL_ARTIFACT_NAME "iTool-${ITOOL_FILENAME_VERSION}${ITOOL_ARTIFACT_SUFFIX}")
set(ITOOL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ITOOL_GENERATED_DIR}")
file(WRITE "${ITOOL_GENERATED_DIR}/artifact-name.txt" "${ITOOL_ARTIFACT_NAME}\n")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/version.h.in"
    "${ITOOL_GENERATED_DIR}/version.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ITOOL_STATIC_THIRD_PARTY
       "Require static wxWidgets and Crypto++ libraries" ON)

if(WIN32)
    enable_language(RC)
endif()

# FindwxWidgets passes this through to wx-config as --static=yes on Unix.
# Set it before either package lookup so a shared build is not selected by
# accident when both variants are installed.
set(wxWidgets_USE_STATIC ${ITOOL_STATIC_THIRD_PARTY})
if(UNIX)
    if(ITOOL_STATIC_THIRD_PARTY)
        set(wxWidgets_CONFIG_OPTIONS --static=yes)
    endif()
endif()

if(UNIX)
    # Use CMake's FindwxWidgets module deliberately: it invokes wx-config and
    # honors wxWidgets_CONFIG_OPTIONS. Some config packages silently expose
    # shared imported targets even when wxWidgets_USE_STATIC is set.
    find_package(wxWidgets 3.2 REQUIRED MODULE COMPONENTS core base net)
    include(${wxWidgets_USE_FILE})
    set(ITOOL_WX_LIBRARIES ${wxWidgets_LIBRARIES})
    set(ITOOL_WX_BASE_LIBRARIES ${wxWidgets_LIBRARIES})
else()
    find_package(wxWidgets 3.2 CONFIG QUIET COMPONENTS core base net)
endif()
if(NOT UNIX AND TARGET wxWidgets::core)
    set(ITOOL_WX_LIBRARIES wxWidgets::core wxWidgets::base wxWidgets::net)
    set(ITOOL_WX_BASE_LIBRARIES wxWidgets::base)
elseif(NOT UNIX)
    # Linux distribution packages and some Homebrew installations expose
    # wxWidgets through wx-config/FindwxWidgets instead of a CMake config.
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base net)
    include(${wxWidgets_USE_FILE})
    set(ITOOL_WX_LIBRARIES ${wxWidgets_LIBRARIES})
    set(ITOOL_WX_BASE_LIBRARIES ${wxWidgets_LIBRARIES})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # wx-config can put -lpthread before its static archives. This happened to
    # work after pthread was merged into glibc 2.34, but fails with the
    # traditional order-sensitive libpthread on Ubuntu 20.04. Keep it last.
    list(REMOVE_ITEM ITOOL_WX_LIBRARIES "-lpthread" "pthread")
    list(REMOVE_ITEM ITOOL_WX_BASE_LIBRARIES "-lpthread" "pthread")
    list(APPEND ITOOL_WX_LIBRARIES "-lpthread")
    list(APPEND ITOOL_WX_BASE_LIBRARIES "-lpthread")
endif()

if(WIN32)
    set(ITOOL_CRYPTOPP_DEFAULT_ROOT "D:/devel/lib/cryptopp890-w32devkit")
else()
    set(ITOOL_CRYPTOPP_DEFAULT_ROOT "")
endif()
set(ITOOL_CRYPTOPP_ROOT "${ITOOL_CRYPTOPP_DEFAULT_ROOT}" CACHE PATH
    "Crypto++ static library installation prefix")
unset(ITOOL_CRYPTOPP_INCLUDE_DIR CACHE)
unset(ITOOL_CRYPTOPP_LIBRARY CACHE)
find_path(ITOOL_CRYPTOPP_INCLUDE_DIR cryptlib.h
          HINTS "${ITOOL_CRYPTOPP_ROOT}/include"
          PATH_SUFFIXES "" cryptopp crypto++ REQUIRED)
if(ITOOL_STATIC_THIRD_PARTY AND UNIX)
    # find_library normally prefers .so/.dylib. Restrict this lookup to the
    # archive so configuration fails early instead of producing a package
    # with an unexpected Crypto++ runtime dependency.
    set(ITOOL_SAVED_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()
find_library(ITOOL_CRYPTOPP_LIBRARY NAMES cryptopp libcryptopp crypto++
             HINTS "${ITOOL_CRYPTOPP_ROOT}/lib" REQUIRED)
if(DEFINED ITOOL_SAVED_FIND_LIBRARY_SUFFIXES)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${ITOOL_SAVED_FIND_LIBRARY_SUFFIXES}")
    unset(ITOOL_SAVED_FIND_LIBRARY_SUFFIXES)
endif()
add_library(CryptoPP::CryptoPP STATIC IMPORTED GLOBAL)
set_target_properties(CryptoPP::CryptoPP PROPERTIES
    IMPORTED_LOCATION "${ITOOL_CRYPTOPP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ITOOL_CRYPTOPP_INCLUDE_DIR}")

add_executable(iTool WIN32 MACOSX_BUNDLE
    src/app/App.cpp
    src/app/MainFrame.cpp
    src/app/MainFrame.h
    src/core/IToolModule.h
    src/core/IStatefulPanel.h
    src/core/Localization.cpp
    src/core/Localization.h
    src/core/AppPaths.cpp
    src/core/AppPaths.h
    src/core/UiState.cpp
    src/core/UiState.h
    src/tools/ascii/AsciiConverterModule.cpp
    src/tools/ascii/AsciiConverterModule.h
    src/tools/ascii/AsciiConverter.cpp
    src/tools/ascii/AsciiConverter.h
    src/tools/base64/Base64ToolModule.cpp
    src/tools/base64/Base64ToolModule.h
    src/tools/barcode/BarcodeToolModule.cpp
    src/tools/barcode/BarcodeToolModule.h
    src/tools/barcode/Code128Encoder.cpp
    src/tools/barcode/Code128Encoder.h
    src/tools/bitset64/Bitset64ToolModule.cpp
    src/tools/bitset64/Bitset64ToolModule.h
    src/tools/crc/CrcToolModule.cpp
    src/tools/crc/CrcToolModule.h
    src/tools/crc/MappedFile.cpp
    src/tools/crc/MappedFile.h
    src/tools/commtest/CommTestToolModule.cpp
    src/tools/commtest/CommTestToolModule.h
    src/tools/comm/PosixSerialPort.h
    src/tools/comm/CommunicationLog.cpp
    src/tools/comm/CommunicationLog.h
    src/tools/comm/MqttProtocol.cpp
    src/tools/comm/MqttProtocol.h
    src/tools/comm/WebSocketProtocol.cpp
    src/tools/comm/WebSocketProtocol.h
    src/tools/commserver/CommServerToolModule.cpp
    src/tools/commserver/CommServerToolModule.h
    src/tools/crypto/CryptoEngine.cpp
    src/tools/crypto/CryptoEngine.h
    src/tools/crypto/CryptoToolModule.cpp
    src/tools/crypto/CryptoToolModule.h
    src/tools/hash/HashToolModule.cpp
    src/tools/hash/HashToolModule.h
    src/tools/linecount/LineCountToolModule.cpp
    src/tools/linecount/LineCountToolModule.h
    src/tools/protocol/ProtocolParser.cpp
    src/tools/protocol/ProtocolParser.h
    src/tools/protocol/ProtocolToolModule.cpp
    src/tools/protocol/ProtocolToolModule.h
    src/tools/qrcode/QrCodeToolModule.cpp
    src/tools/qrcode/QrCodeToolModule.h
    src/tools/replacer/ReplacerToolModule.cpp
    src/tools/replacer/ReplacerToolModule.h
    src/tools/sync/DirectorySyncModule.cpp
    src/tools/sync/DirectorySyncModule.h
    src/tools/time/TimeConverter.cpp
    src/tools/time/TimeConverter.h
    src/tools/time/TimeToolModule.cpp
    src/tools/time/TimeToolModule.h
    src/tools/zeropad/ZeroPadToolModule.cpp
    src/tools/zeropad/ZeroPadToolModule.h
    src/third_party/crc/crc.c
    src/third_party/crc/crc.h
    src/third_party/qrcodegen/qrcodegen.cpp
    src/third_party/qrcodegen/qrcodegen.hpp
)

if(APPLE)
    target_sources(iTool PRIVATE
        resources/macos/itool.icns
        resources/macos/itool.png
    )
    set_source_files_properties(
        resources/macos/itool.icns
        resources/macos/itool.png
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )
elseif(UNIX)
    # A raw ELF file cannot carry a file-manager icon. Generate a launchable
    # desktop entry beside it for the uninstalled build tree, using absolute
    # paths so the icon is resolved without an icon-theme installation.
    set(ITOOL_BUILD_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/${ITOOL_ARTIFACT_NAME}")
    set(ITOOL_BUILD_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/itool.png")
    configure_file(
        resources/linux/iTool-build.desktop.in
        "${CMAKE_BINARY_DIR}/bin/iTool.desktop"
        @ONLY
    )
    configure_file(
        resources/linux/iTool-install.desktop.in
        "${ITOOL_GENERATED_DIR}/iTool.desktop"
        @ONLY
    )
    file(CHMOD "${CMAKE_BINARY_DIR}/bin/iTool.desktop"
         PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    add_custom_command(TARGET iTool POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/itool.png"
                "$<TARGET_FILE_DIR:iTool>/itool.png"
        COMMENT "Copying the Linux application icon"
        VERBATIM
    )
    target_compile_definitions(iTool PRIVATE
        ITOOL_INSTALLED_ICON_PATH="${CMAKE_INSTALL_FULL_DATADIR}/pixmaps/itool.png"
    )
endif()

# crc.c intentionally uses compact aggregate initializers from the supplied
# C implementation; omitted cache fields are zero-initialized by C.
set_source_files_properties(src/third_party/crc/crc.c PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers")

if(WIN32)
    target_sources(iTool PRIVATE src/app/app.rc)
    set_property(SOURCE src/app/app.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/itool.ico")
endif()

target_include_directories(iTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ITOOL_GENERATED_DIR}"
)
target_link_libraries(iTool PRIVATE ${ITOOL_WX_LIBRARIES} CryptoPP::CryptoPP)

# Keep editable catalogs flat in locale/ and generate the conventional gettext
# runtime layout in the build tree.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ITOOL_TRANSLATION_LANGUAGES
    en bg ja cs es et hr it pl pt_BR ru sl tr vi ko zh_CN zh_TW)
set(ITOOL_TRANSLATION_OUTPUTS)
foreach(ITOOL_LANGUAGE IN LISTS ITOOL_TRANSLATION_LANGUAGES)
    set(ITOOL_MO_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/locale/${ITOOL_LANGUAGE}/LC_MESSAGES/iTool.mo")
    add_custom_command(
        OUTPUT "${ITOOL_MO_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${CMAKE_CURRENT_BINARY_DIR}/locale/${ITOOL_LANGUAGE}/LC_MESSAGES"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_mo.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/locale/${ITOOL_LANGUAGE}.po"
                "${ITOOL_MO_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/locale/${ITOOL_LANGUAGE}.po"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_mo.py"
        COMMENT "Compiling ${ITOOL_LANGUAGE} translation catalog"
        VERBATIM
    )
    list(APPEND ITOOL_TRANSLATION_OUTPUTS "${ITOOL_MO_FILE}")
endforeach()
add_custom_target(iTool_translations ALL DEPENDS ${ITOOL_TRANSLATION_OUTPUTS})
add_dependencies(iTool iTool_translations)

add_custom_command(TARGET iTool POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/locale"
            "$<TARGET_FILE_DIR:iTool>/locale"
    COMMENT "Copying iTool translation catalogs"
    VERBATIM
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Ubuntu's file manager classifies PIE binaries as shared libraries and
    # refuses to launch them directly. Emit a conventional ET_EXEC binary;
    # the generated .desktop launcher remains available as the preferred UI.
    target_compile_options(iTool PRIVATE -fno-pie)
    target_link_options(iTool PRIVATE -no-pie)
endif()

if(WIN32)
    target_compile_definitions(iTool PRIVATE
        WINVER=0x0501
        _WIN32_WINNT=0x0501
        NTDDI_VERSION=0x05010300
        UNICODE
        _UNICODE
    )

    if(MINGW)
        target_compile_options(iTool PRIVATE
            -Wall -Wextra -Wpedantic
            -finput-charset=UTF-8
        )
        target_link_options(iTool PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(iTool PRIVATE
            "-Wl,-Map,${CMAKE_BINARY_DIR}/iTool.map")
    endif()
endif()

# UPX remains available for newer Windows targets, but is disabled by default:
# recent UPX stubs are not part of the Windows XP compatibility guarantee.
option(ITOOL_ENABLE_UPX "Compress the Windows executable with UPX" OFF)

# MinGW Release artifacts are finalized as part of the normal CMake build.
if(WIN32 AND MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
    find_program(ITOOL_STRIP_EXECUTABLE NAMES strip
                 HINTS "D:/tools/w64devkit/bin" REQUIRED)
    add_custom_command(TARGET iTool POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                "-DSTRIP_PROGRAM=${ITOOL_STRIP_EXECUTABLE}"
                "-DSTRIP_OPTIONS=--strip-all"
                "-DTARGET_FILE=$<TARGET_FILE:iTool>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StripWithRetry.cmake"
        COMMENT "Stripping symbols from iTool"
        VERBATIM
    )

    if(ITOOL_ENABLE_UPX)
        find_program(ITOOL_UPX_EXECUTABLE NAMES upx
                     HINTS "D:/tools/w64devkit/bin" REQUIRED)
        add_custom_command(TARGET iTool POST_BUILD
            COMMAND "${ITOOL_UPX_EXECUTABLE}" --best --lzma "$<TARGET_FILE:iTool>"
            COMMENT "Compressing iTool with UPX (not recommended for Windows XP)"
            VERBATIM
        )
    endif()
endif()

# Strip release artifacts as part of the normal link step. Apple strip uses a
# different option set from GNU binutils: -x removes all local Mach-O symbols
# while retaining symbols required by the dynamic loader.
if(UNIX AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT CMAKE_STRIP)
        find_program(CMAKE_STRIP NAMES strip REQUIRED)
    endif()
    if(APPLE)
        add_custom_command(TARGET iTool POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DSTRIP_PROGRAM=${CMAKE_STRIP}"
                    "-DSTRIP_OPTIONS=-x"
                    "-DTARGET_FILE=$<TARGET_FILE:iTool>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StripWithRetry.cmake"
            COMMENT "Stripping local symbols from iTool macOS executable"
            VERBATIM
        )
    else()
        add_custom_command(TARGET iTool POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DSTRIP_PROGRAM=${CMAKE_STRIP}"
                    "-DSTRIP_OPTIONS=--strip-all"
                    "-DTARGET_FILE=$<TARGET_FILE:iTool>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StripWithRetry.cmake"
            COMMENT "Stripping all symbols from iTool Linux executable"
            VERBATIM
        )
    endif()
endif()

set_target_properties(iTool PROPERTIES
    OUTPUT_NAME "${ITOOL_ARTIFACT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(APPLE)
    set_target_properties(iTool PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "iTool"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.itool.app"
        MACOSX_BUNDLE_ICON_FILE "itool.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${ITOOL_PLATFORM_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${ITOOL_PLATFORM_VERSION}"
    )
    install(TARGETS iTool BUNDLE DESTINATION ".")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/locale/"
            DESTINATION "iTool.app/Contents/Resources/locale")
elseif(UNIX)
    install(TARGETS iTool RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES resources/linux/itool.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps")
    install(FILES resources/linux/itool.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps")
    install(FILES "${ITOOL_GENERATED_DIR}/iTool.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/locale/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/iTool/locale")
else()
    install(TARGETS iTool RUNTIME DESTINATION ".")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/locale/" DESTINATION "locale")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ascii_converter_test
        tests/ascii_converter_test.cpp
        src/tools/ascii/AsciiConverter.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp)
    target_include_directories(ascii_converter_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(ascii_converter_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    if(WIN32)
        target_compile_definitions(ascii_converter_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME ascii-converter-byte-range COMMAND ascii_converter_test)

    add_test(NAME translation-catalog-coverage
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_translations.py")

    add_executable(directory_sync_test tests/directory_sync_test.cpp)
    target_link_libraries(directory_sync_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    if(WIN32)
        target_compile_definitions(directory_sync_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME directory-sync-recursive-scan COMMAND directory_sync_test)

    add_executable(localization_test
        tests/localization_test.cpp
        src/core/Localization.cpp
        src/core/Localization.h
        src/core/AppPaths.cpp
        src/core/AppPaths.h
    )
    target_include_directories(localization_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(localization_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    add_test(NAME localization-normalization COMMAND localization_test)

    add_executable(crc_vectors_test
        tests/crc_vectors_test.c
        src/third_party/crc/crc.c
    )
    target_include_directories(crc_vectors_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME crc-known-vectors COMMAND crc_vectors_test)

    add_executable(protocol_parser_test
        tests/protocol_parser_test.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp
        src/tools/protocol/ProtocolParser.cpp
        src/tools/protocol/ProtocolParser.h
    )
    target_include_directories(protocol_parser_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(protocol_parser_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    if(WIN32)
        target_compile_definitions(protocol_parser_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME protocol-parser COMMAND protocol_parser_test)

    add_executable(time_converter_test
        tests/time_converter_test.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp
        src/tools/time/TimeConverter.cpp
        src/tools/time/TimeConverter.h
    )
    target_include_directories(time_converter_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(time_converter_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    if(WIN32)
        target_compile_definitions(time_converter_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME time-converter COMMAND time_converter_test)

    add_executable(code128_test
        tests/code128_test.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp
        src/tools/barcode/Code128Encoder.cpp
        src/tools/barcode/Code128Encoder.h
    )
    target_include_directories(code128_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(code128_test PRIVATE ${ITOOL_WX_BASE_LIBRARIES})
    if(WIN32)
        target_compile_definitions(code128_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME code128-encoder COMMAND code128_test)

    add_executable(qrcode_test
        tests/qrcode_test.cpp
        src/third_party/qrcodegen/qrcodegen.cpp
        src/third_party/qrcodegen/qrcodegen.hpp
    )
    target_include_directories(qrcode_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME qrcode-encoder COMMAND qrcode_test)

    add_executable(crypto_engine_test
        tests/crypto_engine_test.cpp
        src/tools/crypto/CryptoEngine.cpp
        src/tools/crypto/CryptoEngine.h
    )
    target_include_directories(crypto_engine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(crypto_engine_test PRIVATE CryptoPP::CryptoPP)
    add_test(NAME crypto-engine COMMAND crypto_engine_test)

    add_executable(websocket_protocol_test
        tests/websocket_protocol_test.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp
        src/tools/comm/WebSocketProtocol.cpp
        src/tools/comm/WebSocketProtocol.h
    )
    target_include_directories(websocket_protocol_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(websocket_protocol_test PRIVATE ${ITOOL_WX_LIBRARIES} CryptoPP::CryptoPP)
    if(WIN32)
        target_compile_definitions(websocket_protocol_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME websocket-protocol COMMAND websocket_protocol_test)

    add_executable(mqtt_protocol_test
        tests/mqtt_protocol_test.cpp
        src/core/Localization.cpp
        src/core/AppPaths.cpp
        src/tools/comm/MqttProtocol.cpp
        src/tools/comm/MqttProtocol.h
    )
    target_include_directories(mqtt_protocol_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(mqtt_protocol_test PRIVATE ${ITOOL_WX_LIBRARIES})
    if(WIN32)
        target_compile_definitions(mqtt_protocol_test PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME mqtt-protocol COMMAND mqtt_protocol_test)
endif()
