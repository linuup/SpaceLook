cmake_minimum_required(VERSION 3.31)

project(SpaceLook LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/FS)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Gui Network Widgets WinExtras Qml Quick QuickWidgets Multimedia MultimediaWidgets Concurrent Xml REQUIRED)

file(GLOB_RECURSE SPACELOOK_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "*.h"
    "*.qrc"
)

list(APPEND SPACELOOK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/ArchiveRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/ArchiveRenderer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/FluentIconFont.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/FluentIconFont.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/PreviewHeaderBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/PreviewHeaderBar.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/SummaryRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/SummaryRenderer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/spacelook.rc"
)

list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/tmp_probe/.*")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/build/.*")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/cmark/.*")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/litehtml/.*")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/ksyntaxhighlighting/src/lib/ansihighlighter\\.(cpp|h)$")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/ksyntaxhighlighting/src/lib/definitiondownloader\\.(cpp|h)$")
list(FILTER SPACELOOK_SOURCES EXCLUDE REGEX ".*/third_party/ksyntaxhighlighting/src/lib/htmlhighlighter\\.(cpp|h)$")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build vendor libraries as static" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable vendor tests" FORCE)
set(CMARK_LIB_FUZZER OFF CACHE BOOL "Disable cmark fuzz targets" FORCE)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "Disable litehtml tests" FORCE)
set(EXTERNAL_GUMBO OFF CACHE BOOL "Use bundled gumbo" FORCE)

add_subdirectory(third_party/cmark EXCLUDE_FROM_ALL)
add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)

add_executable(SpaceLook ${SPACELOOK_SOURCES})

target_sources(SpaceLook PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/document/PreviewHandlerHost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/document/PreviewHandlerHost.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/markup/WebView2HtmlView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/markup/WebView2HtmlView.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/ArchiveRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/FluentIconFont.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/PreviewHeaderBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/summary/SummaryRenderer.cpp"
)

set_source_files_properties(
    third_party/ksyntaxhighlighting/src/lib/ansihighlighter.cpp
    third_party/ksyntaxhighlighting/src/lib/definitiondownloader.cpp
    third_party/ksyntaxhighlighting/src/lib/htmlhighlighter.cpp
    PROPERTIES
        HEADER_FILE_ONLY TRUE
)

set_target_properties(SpaceLook PROPERTIES
    WIN32_EXECUTABLE "$<$<NOT:$<CONFIG:Debug>>:TRUE>"
)

target_link_libraries(SpaceLook
    PRIVATE
        Shell32
        Shlwapi
        Ole32
        OleAut32
        Uuid
        Uiautomationcore
        Crypt32
        gdiplus
        Qt5::Core
        Qt5::Gui
        Qt5::Network
        Qt5::Widgets
        Qt5::WinExtras
        Qt5::Qml
        Qt5::Quick
        Qt5::QuickWidgets
        Qt5::Multimedia
        Qt5::MultimediaWidgets
        Qt5::Concurrent
        Qt5::Xml
        cmark
        litehtml
)

target_compile_definitions(SpaceLook
    PRIVATE
        SPACELOOK_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        "$<$<NOT:$<CONFIG:Debug>>:SPACELOOK_ENABLE_LIBHEIF>"
)

target_include_directories(SpaceLook
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/viewers
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ksyntaxhighlighting/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libheif/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libde265/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/x265/include
)

file(GLOB _QTGUI_PRIVATE_BASE "${Qt5_DIR}/../../../include/QtGui/*")
file(GLOB _QTCORE_PRIVATE_BASE "${Qt5_DIR}/../../../include/QtCore/*")
set(_QTGUI_PRIVATE_VERSION_DIR "")
set(_QTCORE_PRIVATE_VERSION_DIR "")
foreach(_candidate IN LISTS _QTGUI_PRIVATE_BASE)
    if(IS_DIRECTORY "${_candidate}/QtGui/private")
        set(_QTGUI_PRIVATE_VERSION_DIR "${_candidate}")
        break()
    endif()
endforeach()
foreach(_candidate IN LISTS _QTCORE_PRIVATE_BASE)
    if(IS_DIRECTORY "${_candidate}/QtCore/private")
        set(_QTCORE_PRIVATE_VERSION_DIR "${_candidate}")
        break()
    endif()
endforeach()

if(_QTGUI_PRIVATE_VERSION_DIR AND _QTCORE_PRIVATE_VERSION_DIR)
    target_include_directories(SpaceLook
        PRIVATE
            "${_QTGUI_PRIVATE_VERSION_DIR}"
            "${_QTCORE_PRIVATE_VERSION_DIR}"
    )
endif()

target_link_libraries(SpaceLook
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium/lib/pdfium.dll.lib"
        "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/libheif/lib/heif.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/libde265/lib/libde265.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/x265/lib/x265-static.lib>"
)

if(WIN32)
    set(QT_PLUGIN_ROOT "${Qt5_DIR}/../../../plugins")
    set(QT_QML_ROOT "${Qt5_DIR}/../../../qml")
    set(SPACELOOK_QT_IMAGEFORMAT_PLUGINS
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qgifd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qgif.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qicnsd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qicns.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qicod.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qico.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qjpegd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qjpeg.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qsvgd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qsvg.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qtgad.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qtga.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qtiffd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qtiff.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qwbmpd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qwbmp.dll>"
        "$<$<CONFIG:Debug>:${QT_PLUGIN_ROOT}/imageformats/qwebpd.dll>"
        "$<$<NOT:$<CONFIG:Debug>>:${QT_PLUGIN_ROOT}/imageformats/qwebp.dll>"
    )
    set(LIBMPV_DLL_PATH "" CACHE FILEPATH "Path to a libmpv runtime DLL to deploy with SpaceLook")
    set(SPACELOOK_MPV_PART1 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll.part1")
    set(SPACELOOK_MPV_PART2 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll.part2")
    set(SPACELOOK_MPV_PART3 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll.part3")
    set(SPACELOOK_MPV_JOINED_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll")

    if(NOT EXISTS "${SPACELOOK_MPV_JOINED_DLL}" AND EXISTS "${SPACELOOK_MPV_PART1}" AND EXISTS "${SPACELOOK_MPV_PART2}" AND EXISTS "${SPACELOOK_MPV_PART3}")
        message(STATUS "Reconstructing libmpv runtime DLL from split parts")
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
                "$part1 = [System.IO.File]::ReadAllBytes('${SPACELOOK_MPV_PART1}'); $part2 = [System.IO.File]::ReadAllBytes('${SPACELOOK_MPV_PART2}'); $part3 = [System.IO.File]::ReadAllBytes('${SPACELOOK_MPV_PART3}'); $output = New-Object byte[] ($part1.Length + $part2.Length + $part3.Length); [Array]::Copy($part1, 0, $output, 0, $part1.Length); [Array]::Copy($part2, 0, $output, $part1.Length, $part2.Length); [Array]::Copy($part3, 0, $output, $part1.Length + $part2.Length, $part3.Length); [System.IO.File]::WriteAllBytes('${SPACELOOK_MPV_JOINED_DLL}', $output)"
            RESULT_VARIABLE SPACELOOK_MPV_JOIN_RESULT
        )
        if(NOT SPACELOOK_MPV_JOIN_RESULT EQUAL 0)
            message(WARNING "Failed to reconstruct libmpv runtime DLL from split parts.")
        endif()
    endif()

    set(_LIBMPV_CANDIDATE_PATHS
        "${LIBMPV_DLL_PATH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/mpv-2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/mpv.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/libmpv-2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/mpv-2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/libmpv.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/mpv.dll"
        "${CMAKE_SOURCE_DIR}/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/mpv.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/third_party/mpv/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/third_party/mpv/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/third_party/mpv/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/third_party/mpv/mpv.dll"
        "${CMAKE_SOURCE_DIR}/third_party/mpv/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/third_party/mpv/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/third_party/mpv/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/third_party/mpv/mpv.dll"
        "${CMAKE_SOURCE_DIR}/external/mpv/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/external/mpv/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/external/mpv/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/external/mpv/mpv.dll"
        "${CMAKE_SOURCE_DIR}/vendor/mpv/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/vendor/mpv/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/vendor/mpv/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/vendor/mpv/mpv.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/libmpv-2.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/mpv-2.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/libmpv.dll"
        "${CMAKE_SOURCE_DIR}/SpaceLook/mpv.dll"
    )

    set(SPACELOOK_LIBMPV_DLL "")
    foreach(_LIBMPV_CANDIDATE IN LISTS _LIBMPV_CANDIDATE_PATHS)
        if(_LIBMPV_CANDIDATE AND EXISTS "${_LIBMPV_CANDIDATE}")
            set(SPACELOOK_LIBMPV_DLL "${_LIBMPV_CANDIDATE}")
            break()
        endif()
    endforeach()

    if(SPACELOOK_LIBMPV_DLL)
        message(STATUS "SpaceLook libmpv DLL found: ${SPACELOOK_LIBMPV_DLL}")
    else()
        message(STATUS "SpaceLook libmpv DLL not found. Video preview will require manual deployment.")
    endif()

    add_custom_command(TARGET SpaceLook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/mediaservice"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/audio"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/playlistformats"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/imageformats"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/ksyntaxhighlighting"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/ksyntaxhighlighting/syntax"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/ksyntaxhighlighting/themes"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/QtQuick"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_PLUGIN_ROOT}/mediaservice/wmfengine.dll"
            "${QT_PLUGIN_ROOT}/mediaservice/dsengine.dll"
            "$<TARGET_FILE_DIR:SpaceLook>/mediaservice"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_PLUGIN_ROOT}/audio/qtaudio_windows.dll"
            "${QT_PLUGIN_ROOT}/audio/qtaudio_wasapi.dll"
            "$<TARGET_FILE_DIR:SpaceLook>/audio"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_PLUGIN_ROOT}/playlistformats/qtmultimedia_m3u.dll"
            "$<TARGET_FILE_DIR:SpaceLook>/playlistformats"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SPACELOOK_QT_IMAGEFORMAT_PLUGINS}
            "$<TARGET_FILE_DIR:SpaceLook>/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ksyntaxhighlighting/data/syntax"
            "$<TARGET_FILE_DIR:SpaceLook>/ksyntaxhighlighting/syntax"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ksyntaxhighlighting/data/themes"
            "$<TARGET_FILE_DIR:SpaceLook>/ksyntaxhighlighting/themes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQml"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQml"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQuick.2"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQuick.2"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQuick/Controls.2"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQuick/Controls.2"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQuick/Layouts"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQuick/Layouts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQuick/Templates.2"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQuick/Templates.2"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_ROOT}/QtQuick/Window.2"
            "$<TARGET_FILE_DIR:SpaceLook>/QtQuick/Window.2"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:SpaceLook>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium/bin/pdfium.dll"
            "$<TARGET_FILE_DIR:SpaceLook>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libavif/bin/avifdec.exe"
            "$<TARGET_FILE_DIR:SpaceLook>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpaceLook>/7zip"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip/runtime/7z.exe"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip/runtime/7z.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip/runtime/License.txt"
            "$<TARGET_FILE_DIR:SpaceLook>/7zip"
        COMMENT "Copying Qt multimedia plugins for SpaceLook..."
        COMMAND_EXPAND_LISTS
    )

    if(SPACELOOK_LIBMPV_DLL)
        add_custom_command(TARGET SpaceLook POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SPACELOOK_LIBMPV_DLL}"
                "$<TARGET_FILE_DIR:SpaceLook>"
            COMMENT "Copying libmpv runtime for SpaceLook..."
        )
    endif()
endif()
