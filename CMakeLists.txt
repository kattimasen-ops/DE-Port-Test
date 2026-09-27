cmake_minimum_required(VERSION 3.16)
project(de_wrapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Cross-Compile fuer aarch64 ---
if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

# =============================================================
# LTO / IPO (Interprocedural Optimization)
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
# =============================================================
set(CORTEX_FLAGS
    # --- Ziel-CPU ---
    "-mcpu=cortex-a35"
    "-mtune=cortex-a35"

    # --- Optimierungsstufe ---
    "-O3"

    # --- Code-Groesse / Cache-Effizienz ---
    "-fomit-frame-pointer"
    "-fno-stack-protector"
    "-fno-asynchronous-unwind-tables"
    "-fmerge-all-constants"
    "-ffunction-sections"
    "-fdata-sections"

    # --- Symbol-/Call-Overhead reduzieren ---
    "-fno-semantic-interposition"
    "-fno-plt"
    "-fvisibility=hidden"
    "-fvisibility-inlines-hidden"

    # --- C++-Overhead eliminieren ---
    "-fno-exceptions"
    "-fno-rtti"
    "-fno-threadsafe-statics"

    # --- FP / Math ---
    "-fno-math-errno"
    "-fno-trapping-math"
    "-ffast-math"
    "-ftree-vectorize"
    "-fvect-cost-model=unlimited"

    # --- Loop-Optimierungen ---
    "-funroll-loops"
    "-fpeel-loops"
    "-fpredictive-commoning"
    "-fsplit-paths"
    "-ftree-loop-distribute-patterns"
    "-fprefetch-loop-arrays"

    # --- Interprocedural (verstaerkt LTO) ---
    "-fipa-cp-clone"
    "-fdevirtualize-at-ltrans"
    "-fipa-pta"

    # --- Alignment ---
    "-falign-functions=16"
    "-falign-loops=16"
    "-falign-jumps=16"
    "-falign-labels=16"

    # --- Sonstiges ---
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
