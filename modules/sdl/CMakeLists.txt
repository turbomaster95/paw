cmake_minimum_required(VERSION 3.10)

find_package(SDL2 REQUIRED)

include("../LibGen.cmake")

add_mod_shared(sdl
	LINK_LIBRARIES SDL2::SDL2
)
