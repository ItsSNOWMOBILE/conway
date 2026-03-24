cmake_minimum_required(VERSION 3.20)
project(conway VERSION 1.0.0 LANGUAGES CXX)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ────────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
elseif(MSVC)
    add_compile_options(
        /O2
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
    # AVX2 when available
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_options(/arch:AVX2)
    endif()
endif()

# ── Optional: Intel TBB for parallel STL (Linux / macOS) ─────────────────────
if(NOT MSVC)
    find_package(TBB QUIET)
    if(TBB_FOUND)
        message(STATUS "TBB found — parallel step() enabled")
        add_compile_definitions(CONWAY_PARALLEL)
    else()
        message(STATUS "TBB not found — sequential step() (install libtbb-dev to enable parallelism)")
    endif()
else()
    # MSVC ships parallel STL without TBB
    add_compile_definitions(CONWAY_PARALLEL)
endif()

# ── Target ────────────────────────────────────────────────────────────────────
add_executable(conway
    src/main.cpp
    src/grid.cpp
    src/patterns.cpp
    src/renderer.cpp
)

target_include_directories(conway PRIVATE include)

if(TBB_FOUND)
    target_link_libraries(conway PRIVATE TBB::tbb)
endif()

# ── Installation ──────────────────────────────────────────────────────────────
install(TARGETS conway RUNTIME DESTINATION bin)
