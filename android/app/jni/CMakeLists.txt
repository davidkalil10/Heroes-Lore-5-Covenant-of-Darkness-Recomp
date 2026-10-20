cmake_minimum_required(VERSION 3.16)
project(heroes_lore_5_android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# 1. Compilacao do SDL2 para Android
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_ROOT}/third_party/SDL2" SDL2)

# 2. Biblioteca nativa principal (libmain.so) invocada pelo SDLActivity
file(GLOB_RECURSE SRC_FILES
    "${PROJECT_ROOT}/src/*.cpp"
)

add_library(main SHARED ${SRC_FILES})

target_include_directories(main PRIVATE
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/third_party/SDL2/include"
)

target_link_libraries(main PRIVATE
    SDL2
    GLESv2
    log
    android
)
