cmake_minimum_required(VERSION 3.16)
project(de_wrapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Cross-Compile fuer aarch64 ---
if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
    set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()

# --- Executable ---
add_executable(de_wrapper src/wrapper.cpp)

# --- Bibliotheken DIREKT linken (kein find_library!) ---
target_link_libraries(de_wrapper
    dl          # libdl
    pthread     # libpthread
    log         # liblog (Android)
    android     # libandroid (Android)
    EGL         # libEGL
    GLESv2      # libGLESv2
)

target_include_directories(de_wrapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
