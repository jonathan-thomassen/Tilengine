cmake_minimum_required(VERSION 3.16)

project(sc4)

include_directories(../src)

if(UNIX AND NOT APPLE)
  set(CMAKE_C_FLAGS "-m64 -msse2")
elseif(UNIX AND APPLE)
  link_libraries("framework")
endif()

include_directories(../include)
link_libraries("Tilengine")

if(UNIX)
  link_libraries("c")
  link_libraries("m")
endif()

link_libraries("z")
link_libraries("png")
find_package(SDL3 REQUIRED CONFIG)
link_libraries(SDL3::SDL3)

# Compiler-specific flags
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W3)
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
  if(NOT WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpic")
  endif()
  set(CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-format-truncation")
endif()

# Sanitizers: cmake -DSANITIZE=ON ..
option(SANITIZE "Enable AddressSanitizer + UBSan" OFF)
if(SANITIZE AND NOT MSVC)
  message(STATUS "Sanitizers enabled")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g
                      -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

# Copy assets at build time so changes to source assets are reflected on every
# incremental build, not only when CMake is re-configured. The destination is
# removed first so deleted source files don't linger.
add_custom_target(
  copy_sc4_assets ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/assets"
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${CMAKE_CURRENT_BINARY_DIR}/assets"
  COMMENT "Syncing assets to build directory")

add_executable(
  intro
  Intro.c
  Profiling.c
  Simon.c
  SimonCollision.c
  Sandblock.c
  Torch.c
  Prop.c
  Hud.c
  Drawbridge.c
  Whip.c)

# Master list of all sc4 targets, used for dependency wiring below.
set(SC4_TARGETS intro)

# Must wait for assets to be in the build directory.
foreach(SC4_TARGET ${SC4_TARGETS})
  add_dependencies(${SC4_TARGET} copy_sc4_assets)
endforeach()

# Copy required DLLs on MinGW once
if(MINGW)
  if(EXISTS "C:/msys64/mingw64/bin")
    set(MINGW_BIN_DIR "C:/msys64/mingw64/bin")
  endif()

  if(DEFINED MINGW_BIN_DIR)
    add_custom_target(
      copy_sc4_dlls
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN_DIR}/libpng16-16.dll"
        "${MINGW_BIN_DIR}/zlib1.dll" "${MINGW_BIN_DIR}/SDL3.dll"
        "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Copying required DLLs to sc4 directory")

    foreach(SC4_TARGET ${SC4_TARGETS})
      add_dependencies(${SC4_TARGET} copy_sc4_dlls)
    endforeach()
  endif()
endif()
