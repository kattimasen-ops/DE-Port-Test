cmake_minimum_required(VERSION 3.16)
project(de_wrapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

# =============================================================
# LTO / IPO
# =============================================================
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)

if(LTO_SUPPORTED)
    message(STATUS "LTO aktiviert")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    set(CMAKE_AR     "aarch64-linux-gnu-gcc-ar")
    set(CMAKE_RANLIB "aarch64-linux-gnu-gcc-ranlib")
    set(CMAKE_NM     "aarch64-linux-gnu-gcc-nm")
else()
    message(STATUS "LTO NICHT unterstuetzt: ${LTO_ERROR}")
endif()

# =============================================================
# MAXIMALE OPTIMIERUNG (Cortex-A35 / RK3326)
# KEINE funktionsartigen Makros (CMake unterstuetzt das nicht)
# =============================================================
set(CORTEX_FLAGS
    "-mcpu=cortex-a35"
    "-mtune=cortex-a35"
    "-O3"
    "-fomit-frame-pointer"
    "-fno-stack-protector"
    "-fno-asynchronous-unwind-tables"
    "-fmerge-all-constants"
    "-ffunction-sections"
    "-fdata-sections"
    "-fno-semantic-interposition"
    "-fno-plt"
    "-fvisibility=hidden"
    "-fvisibility-inlines-hidden"
    "-fno-exceptions"
    "-fno-rtti"
    "-fno-threadsafe-statics"
    "-fno-math-errno"
    "-fno-trapping-math"
    "-ffast-math"
    "-ftree-vectorize"
    "-fvect-cost-model=unlimited"
    "-funroll-loops"
    "-fpeel-loops"
    "-fpredictive-commoning"
    "-fsplit-paths"
    "-ftree-loop-distribute-patterns"
    "-fprefetch-loop-arrays"
    "-fipa-cp-clone"
    "-fdevirtualize-at-ltrans"
    "-fipa-pta"
    "-falign-functions=16"
    "-falign-loops=16"
    "-falign-jumps=16"
    "-falign-labels=16"
    "-fno-ident"
    "-DNDEBUG"
)

if(LTO_SUPPORTED)
    list(APPEND CORTEX_FLAGS
        "-flto=auto"
        "-fno-fat-lto-objects"
        "-fuse-linker-plugin"
    )
endif()

string(REPLACE ";" " " OPTIMIZE_FLAGS "${CORTEX_FLAGS}")

set(CMAKE_C_FLAGS   "${OPTIMIZE_FLAGS}")
set(CMAKE_CXX_FLAGS "${OPTIMIZE_FLAGS}")

if(LTO_SUPPORTED)
    set(CMAKE_EXE_LINKER_FLAGS
        "-flto=auto -fuse-linker-plugin -Wl,-O1 -Wl,--as-needed -Wl,--gc-sections"
    )
else()
    set(CMAKE_EXE_LINKER_FLAGS
        "-Wl,-O1 -Wl,--as-needed -Wl,--gc-sections"
    )
endif()

# =============================================================
# Executable
# =============================================================
add_executable(de_wrapper src/wrapper.cpp)

target_link_libraries(de_wrapper
    dl
    pthread
)

target_include_directories(de_wrapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
