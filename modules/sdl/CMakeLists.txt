cmake_minimum_required(VERSION 3.16)

find_package(SDL3 QUIET CONFIG)
if(SDL3_FOUND)
    set(SDL_BACKEND     SDL3)
    set(SDL_LINK_TARGET SDL3::SDL3)
else()
    find_package(SDL2 REQUIRED)
    set(SDL_BACKEND     SDL2)
    set(SDL_LINK_TARGET SDL2::SDL2)
endif()

message(STATUS "paw module 'sdl': linking against ${SDL_BACKEND}")

include("${CMAKE_CURRENT_LIST_DIR}/../LibGen.cmake")

add_mod_shared(sdl
    LINK_LIBRARIES ${SDL_LINK_TARGET}
)

if(SDL3_FOUND)
    target_compile_definitions(sdl PRIVATE PAW_SDL_USE_SDL3)
endif()
