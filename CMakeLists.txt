cmake_minimum_required(VERSION 3.16)
project(SharpMark)

# Define ANSI escape sequences for colored output (works in most terminals)
if(NOT WIN32 OR (DEFINED ENV{TERM} AND NOT "$ENV{TERM}" STREQUAL ""))
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorRed   "${Esc}[31m")
    set(ColorGreen "${Esc}[32m")
    set(ColorBold  "${Esc}[1m")
else()
    set(ColorReset "")
    set(ColorRed   "")
    set(ColorGreen "")
    set(ColorBold  "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt Auto-Moc, Auto-Uic, and Auto-Rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Default to Release build for performance (-O2)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags for GCC/Clang
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -march=native")
endif()

# Debug benchmark option
option(ENABLE_BENCHMARK "Enable debug benchmark output" OFF)
if(ENABLE_BENCHMARK)
    add_definitions(-DDEBUG_BENCHMARK)
endif()

# Try to find libraries (Linux/vcpkg/MSYS2 approach)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Find Qt6 Components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
endif()

if(NOT LIBRAW_FOUND)
    find_path(LIBRAW_INCLUDE_DIRS NAMES libraw/libraw.h)
    find_library(LIBRAW_LIBRARIES NAMES raw)
    if(LIBRAW_INCLUDE_DIRS AND LIBRAW_LIBRARIES)
        set(LIBRAW_FOUND TRUE)
    endif()
endif()

if(NOT LIBRAW_FOUND)
    message(FATAL_ERROR "${ColorRed}${ColorBold}LibRaw not found!${ColorReset}")
else()
    message(STATUS "${ColorGreen}LibRaw found.${ColorReset}")
endif()

message(STATUS "${ColorGreen}Qt6 found.${ColorReset}")

# Source files
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
    add_executable(SharpMark WIN32 ${SOURCES})
else()
    add_executable(SharpMark ${SOURCES})
endif()

target_include_directories(SharpMark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(TARGET PkgConfig::LIBRAW)
    target_link_libraries(SharpMark PRIVATE PkgConfig::LIBRAW)
else()
    target_include_directories(SharpMark PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_link_libraries(SharpMark PRIVATE ${LIBRAW_LIBRARIES})
endif()

# Link Qt libraries
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

# --- QML Module Setup ---
qt_add_qml_module(SharpMark
    URI SharpMark
    VERSION 1.0
    QML_FILES
        qml/main.qml
)

# --- ONNX Runtime (Local Binaries) ---
set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime")

if(EXISTS "${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "${ColorGreen}ONNX Runtime found locally at ${ONNXRUNTIME_DIR}${ColorReset}")
    
    # Include headers
    target_include_directories(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/include")
    
    # Link the library depending on OS
    if(WIN32)
        # Windows: Link against the .lib file
        target_link_libraries(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
        
        # We also need to copy the DLL to the build folder so the app runs during dev
        add_custom_command(TARGET SharpMark POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            $<TARGET_FILE_DIR:SharpMark>
        )
    elseif(UNIX AND NOT APPLE)
        # Linux: Link against the .so file
        target_link_libraries(SharpMark PRIVATE "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
        
        # Set RPATH so it finds the .so locally
        set_target_properties(SharpMark PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()

    add_definitions(-DUSE_ONNXRUNTIME)
else()
    message(WARNING "${ColorRed}ONNX Runtime local folder not found! AI features disabled.${ColorReset}")
endif()

if(UNIX AND NOT APPLE)
    # Install executable in /usr/local/bin 
    install(TARGETS SharpMark DESTINATION bin)

    # Install .desktop in app menu
    install(FILES assets/sharpmark.desktop 
            DESTINATION share/applications)

    # Install icon in system theme hicolor 
    install(FILES assets/icons/256x256/icon.png 
            DESTINATION share/icons/hicolor/256x256/apps 
            RENAME sharpmark.png)
endif()

if(NOT TARGET uninstall)
    configure_file(
        "cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()

# --- CPack Configuration for Debian Package ---
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    
    # Debian package names are typically lowercase
    set(CPACK_PACKAGE_NAME "sharpmark") 
    set(CPACK_PACKAGE_VERSION "2.0.0-alpha.1")
    
    # Required field for .deb packages (maintainer info)
    set(CPACK_PACKAGE_CONTACT "BlueTuna <azimuthsouth5@gmail.com>") 
    
    # Short description extracted from your .desktop file
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast Image Sharpness Analyzer and Culling Tool")
    
    # Enable automatic dependency detection
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    
    include(CPack)
endif()