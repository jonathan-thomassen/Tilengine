cmake_minimum_required(VERSION 3.16)

project(Tilengine)

# Generate compile_commands.json for IDE/language server support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add cmake modules directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Set C standard early so it applies to all targets, including the library.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB SOURCES
     "src/*.c"
)
file(GLOB INCLUDE_SOURCES
     "include/*.c"
)


# main library
list(REMOVE_ITEM SOURCES "src/Test.c")
add_library(${PROJECT_NAME} ${SOURCES} ${INCLUDE_SOURCES})

# Architecture flags: apply on any x86-64 target with a GCC-compatible compiler.
# CMAKE_SYSTEM_PROCESSOR is "x86_64" on Linux/macOS and "AMD64" on Windows.
if (NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -msse2")
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "c")
elseif (UNIX AND APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "framework")
    target_link_libraries(${PROJECT_NAME} PRIVATE "c")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE "z")
target_link_libraries(${PROJECT_NAME} PRIVATE "png")

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PUBLIC "winmm")
endif()

# On MinGW, prefer pkg-config for SDL3
if (MINGW)
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(SDL3 sdl3)
        if (SDL3_FOUND)
            target_include_directories(${PROJECT_NAME} PRIVATE ${SDL3_INCLUDE_DIRS})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL3_LIBRARIES})
        endif()
    endif()
endif()

# Fallback to CONFIG mode if not found via pkg-config
if (NOT SDL3_FOUND)
    find_package(SDL3 REQUIRED CONFIG)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE include)

# Compiler-specific flags
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes")
    if (NOT WIN32)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpic")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-format-truncation")
endif()

# Sanitizers: cmake -DSANITIZE=ON ..
option(SANITIZE "Enable AddressSanitizer + UBSan" OFF)
if (SANITIZE AND NOT MSVC)
    message(STATUS "Sanitizers enabled")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
endif()

# test executable
add_executable(Test src/Test.c)
target_include_directories(Test PRIVATE include)
target_link_libraries(Test PRIVATE "Tilengine")

if (UNIX)
    target_link_libraries(Test PRIVATE "c")
    target_link_libraries(Test PRIVATE "m")
endif()

target_link_libraries(Test PRIVATE "z")
target_link_libraries(Test PRIVATE "png")

# Copy required DLLs on MinGW after building
if (MINGW)
    # Allow users to override the MinGW bin directory; auto-detect as fallback.
    set(MINGW_BIN_DIR "" CACHE PATH "Path to the MinGW/MSYS2 bin directory containing SDL3.dll etc.")
    if (NOT MINGW_BIN_DIR AND EXISTS "C:/msys64/mingw64/bin")
        set(MINGW_BIN_DIR "C:/msys64/mingw64/bin")
    endif()
    
    if (DEFINED MINGW_BIN_DIR)
        # Copy DLLs after building Test executable
        add_custom_command(TARGET Test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libpng16-16.dll"
                "${MINGW_BIN_DIR}/zlib1.dll"
                "${MINGW_BIN_DIR}/SDL3.dll"
                $<TARGET_FILE_DIR:Test>
            COMMENT "Copying required DLLs to build directory"
        )
    endif()
endif()

# samples
add_subdirectory(samples)
add_subdirectory(sc4)
