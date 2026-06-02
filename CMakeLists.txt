cmake_minimum_required(VERSION 3.16)
project(SharpMark)

add_compile_options(-w)
add_compile_definitions(WIN32_LEAN_AND_MEAN)

# ==========================================
# Compiler & Terminal Output Setup
# ==========================================
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_COLOR_MAKEFILE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fdiagnostics-color=always)
endif()

if(NOT WIN32 OR (DEFINED ENV{TERM} AND NOT "$ENV{TERM}" STREQUAL ""))
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorRed   "${Esc}[31m")
    set(ColorGreen "${Esc}[32m")
else()
    set(ColorReset "")
    set(ColorRed   "")
    set(ColorGreen "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -march=native")
endif()

option(ENABLE_BENCHMARK "Enable debug benchmark output" OFF)
if(ENABLE_BENCHMARK)
    add_definitions(-DDEBUG_BENCHMARK)
endif()

# ==========================================
# Dependencies
# ==========================================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
message(STATUS "${ColorGreen}Qt6 found.${ColorReset}")

# LibRaw (Cross-Platform)
if(WIN32)
    find_package(libraw NAMES libraw LibRaw REQUIRED)
    message(STATUS "${ColorGreen}LibRaw found via vcpkg.${ColorReset}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)
    message(STATUS "${ColorGreen}LibRaw found via pkg-config.${ColorReset}")
endif()

# ==========================================
# Target Definition
# ==========================================
set(SOURCES
    src/main.cpp
    src/app_backend.cpp
    src/img_tools/laplacian.cpp
    src/img_tools/bmp.cpp
    src/tools/scan.cpp
    src/tools/XMP_tools.cpp
    src/processors/clip_embedding.cpp
    src/processors/aesthetic_scorer.cpp
    src/gui/utils/path_utils.cpp
)

if(WIN32)
    list(APPEND SOURCES assets/icons/app_icon.rc)
    qt_add_executable(SharpMark WIN32 ${SOURCES})
else()
    qt_add_executable(SharpMark ${SOURCES})
endif()

target_include_directories(SharpMark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Linking LibRaw
if(WIN32)
    target_link_libraries(SharpMark PRIVATE libraw::raw)
else()
    target_link_libraries(SharpMark PRIVATE PkgConfig::LIBRAW)
endif()

# Linking Qt & Threads
target_link_libraries(SharpMark PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Qml 
    Qt6::Quick
    Threads::Threads
)

if(UNIX)
    target_link_libraries(SharpMark PRIVATE m)
endif()

qt_add_qml_module(SharpMark
    URI SharpMark
    VERSION 1.0
    QML_FILES qml/main.qml
)

# ==========================================
# ONNX Runtime Integration
# ==========================================
set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime")

if(EXISTS "${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "${ColorGreen}ONNX Runtime found locally.${ColorReset}")
    target_include_directories(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/include")
    
    if(WIN32)
        target_link_libraries(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
        # Copy DLL during dev builds so debugging works
        add_custom_command(TARGET SharpMark POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            $<TARGET_FILE_DIR:SharpMark>
        )
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
        set_target_properties(SharpMark PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()

    add_definitions(-DUSE_ONNXRUNTIME)
else()
    message(WARNING "${ColorRed}ONNX Runtime not found! AI features disabled.${ColorReset}")
endif()

# ==========================================
# Deployment Setup (Windows Portable vs Linux)
# ==========================================
if(WIN32)
    # --- Windows Portable Deployment ---
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/SharpMark_Portable" CACHE PATH "Portable Install Dir" FORCE)
    #if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    #    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/SharpMark_Portable" CACHE PATH "Portable Install Dir" FORCE)
    #endif()

    # Tell vcpkg to automatically copy all sub-dependencies
    set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

    # 1. Hide the core executable
    set_target_properties(SharpMark PROPERTIES OUTPUT_NAME "SharpMark-core")

    # 2. Generate the C-Launcher (Now pointing to the 'bin' folder!)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launcher.c"
        "#include <windows.h>\n"
        "int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {\n"
        "    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;\n"
        "    if (!CreateProcessW(L\"bin\\\\SharpMark-core.exe\", GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {\n"
        "        MessageBoxW(NULL, L\"Failed to start bin\\\\SharpMark-core.exe\", L\"Launch Error\", MB_ICONERROR);\n"
        "        return 1;\n"
        "    }\n"
        "    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); return 0;\n"
        "}\n"
    )
    
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/icons/app_icon.rc")
        add_executable(SharpMark_Launcher WIN32 "${CMAKE_CURRENT_BINARY_DIR}/launcher.c" "${CMAKE_SOURCE_DIR}/assets/icons/app_icon.rc")
    else()
        add_executable(SharpMark_Launcher WIN32 "${CMAKE_CURRENT_BINARY_DIR}/launcher.c")
    endif()
    set_target_properties(SharpMark_Launcher PROPERTIES OUTPUT_NAME "SharpMark")

    # 3. Fetch External Tools
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/exiftool/exiftool.exe")
        message(STATUS "Downloading ExifTool from SourceForge...")
        file(DOWNLOAD "https://downloads.sourceforge.net/project/exiftool/exiftool-13.59_64.zip" 
                      "${CMAKE_CURRENT_BINARY_DIR}/exiftool.zip" SHOW_PROGRESS TIMEOUT 120)
        file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_BINARY_DIR}/exiftool.zip" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/exiftool_ext")
        file(GLOB EXIFTOOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/exiftool_ext/exiftool-*")
        file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/exiftool")
        file(RENAME "${EXIFTOOL_DIR}/exiftool(-k).exe" "${CMAKE_SOURCE_DIR}/assets/exiftool/exiftool.exe")
        file(COPY "${EXIFTOOL_DIR}/exiftool_files" DESTINATION "${CMAKE_SOURCE_DIR}/assets/exiftool")
    endif()
    
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/models/vision_model_quantized.onnx")
        message(STATUS "Downloading AI Model...")
        file(DOWNLOAD "https://huggingface.co/Xenova/clip-vit-base-patch32/resolve/main/onnx/model_quantized.onnx"
                      "${CMAKE_SOURCE_DIR}/assets/models/vision_model_quantized.onnx" SHOW_PROGRESS TIMEOUT 300)
    endif()

    # ------------------------------------------
    # Installation Routing (Everything to 'bin')
    # ------------------------------------------
    
    # Put Core App inside 'bin' and Launcher at root '.'
    install(TARGETS SharpMark RUNTIME DESTINATION bin)
    install(TARGETS SharpMark_Launcher RUNTIME DESTINATION .)

    # Let Qt deploy all its DLLs and QML plugins natively to 'bin'
    qt_generate_deploy_qml_app_script(
        TARGET SharpMark
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        DEPLOY_TOOL_OPTIONS "--dir" "bin"
    )
    install(SCRIPT ${deploy_script})

    # Route Exiftool, ONNX Model, and ONNX DLL to 'bin'
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/assets/exiftool/exiftool.exe" DESTINATION bin)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/exiftool/exiftool_files" DESTINATION bin)
    install(FILES "${CMAKE_SOURCE_DIR}/assets/models/vision_model_quantized.onnx" DESTINATION bin)
    
    if(EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
        install(FILES "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll" DESTINATION bin)
    endif()

    # Explicitly copy LibRaw and ALL its transitive dependencies 
    # (lcms2-2, z, jpeg, tiff, etc.) directly from the vcpkg bin folder.
    if(libraw_DIR)
        get_filename_component(VCPKG_ROOT_DIR "${libraw_DIR}/../.." ABSOLUTE)
        
        # Glob ALL .dll files in the vcpkg bin directory
        file(GLOB VCPKG_DLLS "${VCPKG_ROOT_DIR}/bin/*.dll")
        
        if(VCPKG_DLLS)
            install(FILES ${VCPKG_DLLS} DESTINATION bin)
        endif()
    endif()

else()
    # --- Linux Standard System Deployment ---
    install(TARGETS SharpMark DESTINATION bin)
    install(FILES assets/sharpmark.desktop DESTINATION share/applications)
    install(FILES assets/icons/256x256/icon.png DESTINATION share/icons/hicolor/256x256/apps RENAME sharpmark.png)

    # Uninstall Target
    if(NOT TARGET uninstall)
        configure_file("cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
        add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
    endif()

    # CPack (Debian Package Generator)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "sharpmark") 
    set(CPACK_PACKAGE_VERSION "2.0.0-alpha.1")
    set(CPACK_PACKAGE_CONTACT "x") 
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast Image Sharpness Analyzer and Culling Tool")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    include(CPack)
endif()