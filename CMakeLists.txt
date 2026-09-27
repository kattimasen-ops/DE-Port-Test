cmake_minimum_required(VERSION 3.16)
project(de_wrapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Cross-Compile fuer aarch64 ---
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_CROSSCOMPILING)
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

# --- Bibliotheken ---
find_library(DL_LIB dl)
find_library(PTHREAD_LIB pthread)
find_library(LOG_LIB log)
find_library(ANDROID_LIB android)
find_library(EGL_LIB EGL)
find_library(GLES_LIB GLESv2)

# --- Executable ---
add_executable(de_wrapper src/wrapper.cpp)

target_link_libraries(de_wrapper
    ${DL_LIB}
    ${PTHREAD_LIB}
    ${LOG_LIB}
    ${ANDROID_LIB}
    ${EGL_LIB}
    ${GLES_LIB}
)

target_include_directories(de_wrapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
