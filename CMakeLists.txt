cmake_minimum_required(VERSION 3.16)
project(de_wrapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

add_executable(de_wrapper src/wrapper.cpp)

# NUR Standard-Linux-Bibliotheken linken.
# Die Android-Libs werden zur Laufzeit per dlopen() geladen,
# weil sie bionic-gelinkt sind und nicht direkt gelinkt werden koennen.
target_link_libraries(de_wrapper
    dl
    pthread
)

target_include_directories(de_wrapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
