cmake_minimum_required(VERSION 3.10)

project(samples)


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
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -O2 -fpic")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-format-truncation")
endif()

file(COPY assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

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

# Copy required DLLs on MinGW after building samples
if (MINGW)
    # Find MSYS2 MinGW bin directory
    if (EXISTS "C:/msys64/mingw64/bin")
        set(MINGW_BIN_DIR "C:/msys64/mingw64/bin")
    endif()
    
    if (DEFINED MINGW_BIN_DIR)
        # List of all sample executables
        set(SAMPLE_TARGETS 
            barrel mode7 platformer racer scaling shadow shooter 
            tutorial wobble colorcycle benchmark supermarioclone 
            test_mouse forest querylayer
        )
        
        # Copy DLLs after building each sample
        foreach(SAMPLE_TARGET ${SAMPLE_TARGETS})
            add_custom_command(TARGET ${SAMPLE_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libpng16-16.dll"
                    "${MINGW_BIN_DIR}/zlib1.dll"
                    "${MINGW_BIN_DIR}/SDL3.dll"
                    $<TARGET_FILE_DIR:${SAMPLE_TARGET}>
                COMMENT "Copying required DLLs to samples directory"
            )
        endforeach()
    endif()
endif()

