cmake_minimum_required(VERSION 3.25)
project(libmobile-bgb VERSION 0.2.0)

include(CheckLibraryExists)
include(CheckLinkerFlag)

set(CMAKE_C_STANDARD 11)
option(WITH_SYSTEM_LIBMOBILE "force using a system-wide copy of libmobile" OFF)
option(WITH_BUNDLED_LIBMOBILE "force using a bundled copy of libmobile" OFF)

set(c_args)
set(c_defs)
set(deps)

# Default cflags
if(NOT MSVC)
    list(APPEND c_args -Wall -Wextra)
endif()

# Enable -ffunction-sections -fdata-sections and -Wl,--gc-sections by default
check_linker_flag(C -Wl,--gc-sections HAVE_GC_SECTIONS)
if(HAVE_GC_SECTIONS)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

find_package(PkgConfig)

# Include libmobile library
if(NOT WITH_BUNDLED_LIBMOBILE AND PkgConfig_FOUND)
    pkg_check_modules(libmobile IMPORTED_TARGET "libmobile >= 0.2.0")
endif()
if (WITH_SYSTEM_LIBMOBILE AND NOT libmobile_FOUND)
    message(FATAL_ERROR "libmobile not found")
endif()
if(libmobile_FOUND)
    list(APPEND deps PkgConfig::libmobile)
else()
    add_subdirectory(subprojects/libmobile)
    list(APPEND deps libmobile_static)
endif()

if(WIN32)
    check_library_exists(ws2_32 exit "" HAVE_LIBWS2_32)
    if(NOT HAVE_LIBWS2_32)
        message(FATAL_ERROR "ws2_32 not found")
    endif()
    list(APPEND deps ws2_32)
    list(APPEND c_defs UNICODE _UNICODE _WIN32_WINNT=0x0501)
    list(APPEND c_defs _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(mobile
    source/bgblink.c
    source/bgblink.h
    source/main.c
    source/socket.c
    source/socket.h
    source/socket_impl.c
    source/socket_impl.h)
target_link_libraries(mobile PRIVATE ${deps})
target_compile_options(mobile PRIVATE ${c_args})
target_compile_definitions(mobile PRIVATE ${c_defs})
install(TARGETS mobile)
