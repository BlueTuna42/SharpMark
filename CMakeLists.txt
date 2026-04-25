cmake_minimum_required(VERSION 3.10)
project(FocusChecker)

# Define ANSI escape sequences for colored output
if(NOT WIN32 OR (DEFINED ENV{TERM} AND NOT "$ENV{TERM}" STREQUAL ""))
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorRed   "${Esc}[31m")
    set(ColorGreen "${Esc}[32m")
    set(ColorBold  "${Esc}[1m")
else()
    # Fallback for Windows consoles that might not support ANSI
    set(ColorReset "")
    set(ColorRed   "")
    set(ColorGreen "")
    set(ColorBold  "")
endif()

# Use C++17 standard for std::filesystem support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for Release build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Explicitly add -O2 and -march=native for performance (non-MSVC compilers)
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -march=native")
endif()

# Option to enable benchmark mode (disabled by default)
option(ENABLE_BENCHMARK "Enable debug benchmark output" OFF)
if(ENABLE_BENCHMARK)
    add_definitions(-DDEBUG_BENCHMARK)
endif()

# Find Threads (cross-platform)
find_package(Threads REQUIRED)

# Try to find libraries using PkgConfig first (good for Linux)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFTW3 IMPORTED_TARGET fftw3f)
    pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
endif()

# Fallback manual search if PkgConfig failed (better for Windows/Mac)
if(NOT FFTW3_FOUND)
    find_path(FFTW3_INCLUDE_DIRS NAMES fftw3.h)
    find_library(FFTW3_LIBRARIES NAMES fftw3f)
    if(FFTW3_INCLUDE_DIRS AND FFTW3_LIBRARIES)
        set(FFTW3_FOUND TRUE)
    endif()
endif()

if(NOT LIBRAW_FOUND)
    find_path(LIBRAW_INCLUDE_DIRS NAMES libraw/libraw.h)
    find_library(LIBRAW_LIBRARIES NAMES raw)
    if(LIBRAW_INCLUDE_DIRS AND LIBRAW_LIBRARIES)
        set(LIBRAW_FOUND TRUE)
    endif()
endif()

# Mandatory check: Stop if libraries are missing
if(NOT FFTW3_FOUND)
    message(FATAL_ERROR "${ColorRed}${ColorBold}FFTW3 (float version) not found! Please install it.${ColorReset}")
else()
    message(STATUS "${ColorGreen}FFTW3 (float version) found.${ColorReset}")
endif()

if(NOT LIBRAW_FOUND)
    message(FATAL_ERROR "${ColorRed}${ColorBold}LibRaw not found! Please install it.${ColorReset}")
else()
    message(STATUS "${ColorGreen}LibRaw found.${ColorReset}")
endif()

# Check for exiftool (runtime dependency)
find_program(EXIFTOOL_EXECUTABLE exiftool)
if(NOT EXIFTOOL_EXECUTABLE)
    message(WARNING "${ColorRed}${ColorBold}exiftool not found in PATH! Rating features will fail at runtime.${ColorReset}")
else()
    message(STATUS "${ColorGreen}exiftool found.${ColorReset}")
endif()

# List of source files
set(SOURCES
    main.cpp
    FFT.cpp
    bmp.cpp
    scan.cpp
    XMP_tools.cpp
)

# Create executable
add_executable(focus_checker ${SOURCES})

# Configure include directories
if(TARGET PkgConfig::FFTW3)
    target_link_libraries(focus_checker PRIVATE PkgConfig::FFTW3)
else()
    target_include_directories(focus_checker PRIVATE ${FFTW3_INCLUDE_DIRS})
    target_link_libraries(focus_checker PRIVATE ${FFTW3_LIBRARIES})
endif()

if(TARGET PkgConfig::LIBRAW)
    target_link_libraries(focus_checker PRIVATE PkgConfig::LIBRAW)
else()
    target_include_directories(focus_checker PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_link_libraries(focus_checker PRIVATE ${LIBRAW_LIBRARIES})
endif()

target_include_directories(focus_checker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(focus_checker PRIVATE Threads::Threads)

# Math library link for Unix systems
if(UNIX)
    target_link_libraries(focus_checker PRIVATE m)
endif()