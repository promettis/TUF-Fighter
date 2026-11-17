# =============================================================================
#  TUF FIGHTER - build file
#  Works with Visual Studio (MSVC) and MinGW on Windows, and with GCC on Linux.
#
#      cmake -S . -B build
#      cmake --build build --config Release
#
#  The executable is written next to this file (TUF_Fighter.exe) so it finds
#  the Assets folder.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(TUF_Fighter C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# link the C/C++ runtime statically so the .exe runs on any PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- raylib (graphics / input / audio library), built from source ----
set(RL third_party/raylib_src)
add_library(raylib STATIC
    ${RL}/rcore.c ${RL}/rshapes.c ${RL}/rtextures.c ${RL}/rtext.c
    ${RL}/rmodels.c ${RL}/raudio.c ${RL}/rglfw.c)
target_include_directories(raylib PUBLIC ${RL} PRIVATE ${RL}/external/glfw/include)
target_compile_definitions(raylib PUBLIC PLATFORM_DESKTOP_GLFW GRAPHICS_API_OPENGL_33 SUPPORT_FILEFORMAT_JPG=1)
if (MSVC)
    target_compile_options(raylib PRIVATE /W0)
    target_compile_definitions(raylib PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(raylib PRIVATE -w)
    set_property(TARGET raylib PROPERTY C_STANDARD 99)
    target_compile_definitions(raylib PRIVATE _GNU_SOURCE)
endif()
if (WIN32)
    target_link_libraries(raylib PUBLIC winmm gdi32 opengl32 shell32)
else()
    target_compile_definitions(raylib PRIVATE _GLFW_X11)
    target_link_libraries(raylib PUBLIC GL m pthread dl rt X11)
endif()

# ---- the game ----
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
file(GLOB MESHOPT_SOURCES third_party/meshopt/*.cpp)
add_executable(TUF_Fighter ${GAME_SOURCES} ${MESHOPT_SOURCES})
target_include_directories(TUF_Fighter PRIVATE src third_party/meshopt)
target_link_libraries(TUF_Fighter PRIVATE raylib)
if (MSVC)
    target_compile_options(TUF_Fighter PRIVATE /utf-8 /W3 /wd4244 /wd4305 /wd4267 /wd4996)
    # no console window
    target_link_options(TUF_Fighter PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
elseif (WIN32)
    target_link_options(TUF_Fighter PRIVATE -static -mwindows)
endif()

# put the .exe next to the Assets folder
set_target_properties(TUF_Fighter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
    DEBUG_POSTFIX "_debug"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TUF_Fighter)
