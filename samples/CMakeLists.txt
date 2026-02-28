cmake_minimum_required(VERSION 3.16)

project(samples)

include_directories(../src)

if (UNIX AND NOT APPLE)
	set(CMAKE_C_FLAGS "-m64 -msse2")
elseif (UNIX AND APPLE)
    link_libraries("framework")
endif()

include_directories(../include)
link_libraries("Tilengine")

if (UNIX)
    link_libraries("c")
    link_libraries("m")
endif()

link_libraries("z")
link_libraries("png")
find_package(SDL3 REQUIRED CONFIG)
link_libraries(SDL3::SDL3)

# Compiler-specific flags
if (MSVC)
    add_compile_options(/W3)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c23 -O2")
    if (NOT WIN32)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpic")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-format-truncation")
endif()

# Copy assets at build time so changes to source assets are reflected
# on every incremental build, not only when CMake is re-configured.
# The destination is removed first so deleted source files don't linger.
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMENT "Syncing assets to build directory"
)

add_executable(intro 
     Intro.c
     PaletteLayer.c
     Simon.c
     Sandblock.c)

add_executable(barrel 
     Barrel.c
     Simon.c)

add_executable(mode7
     Mode7.c
     Sin.c)

add_executable(platformer
     Platformer.c)

add_executable(racer
     Racer.c
     Tree.c
     Actor.c)

add_executable(scaling
     Scaling.c)

add_executable(shadow
     Shadow.c)

add_executable(shooter
     Shooter.c
     Actor.c
     Boss.c
     Enemy.c
     Explosion.c
     Ship.c
     Sin.c)

add_executable(tutorial
     Tutorial.c)

add_executable(wobble
     Wobble.c
     Sin.c)

add_executable(colorcycle
     ColorCycle.c)

add_executable(benchmark
     Benchmark.c)

add_executable(supermarioclone
     SuperMarioClone.c)

add_executable(test_mouse
     TestMouse.c)

add_executable(forest
     Forest.c)

add_executable(querylayer
     QueryLayer.c)

add_executable(layerwindow
     LayerWindow.c)

add_executable(layercircle
     LayerCircle.c)

# Master list of all sample targets, used for dependency wiring below.
set(SAMPLE_TARGETS
    intro barrel mode7 platformer racer scaling shadow shooter
    tutorial wobble colorcycle benchmark supermarioclone
    test_mouse forest querylayer layerwindow layercircle
)

# Every sample must wait for assets to be in the build directory.
foreach(SAMPLE_TARGET ${SAMPLE_TARGETS})
    add_dependencies(${SAMPLE_TARGET} copy_assets)
endforeach()

# Copy required DLLs on MinGW once for all samples
if (MINGW)
    if (EXISTS "C:/msys64/mingw64/bin")
        set(MINGW_BIN_DIR "C:/msys64/mingw64/bin")
    endif()

    if (DEFINED MINGW_BIN_DIR)
        add_custom_target(copy_sample_dlls
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libpng16-16.dll"
                "${MINGW_BIN_DIR}/zlib1.dll"
                "${MINGW_BIN_DIR}/SDL3.dll"
                "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Copying required DLLs to samples directory"
        )

        foreach(SAMPLE_TARGET ${SAMPLE_TARGETS})
            add_dependencies(${SAMPLE_TARGET} copy_sample_dlls)
        endforeach()
    endif()
endif()

