cmake_minimum_required(VERSION 3.11)
set(PAW_TARGET "native" CACHE STRING "Target architecture (native|wasi)")
set(NEED_DBG "no" CACHE STRING "Enable Debug symbols (yes|no)")
set(NEED_ASAN "" CACHE STRING "Enable ASAN (yes|no)")

if("${PAW_TARGET}" STREQUAL "wasi")
    set(CMAKE_SYSROOT "/usr/share/wasi-sysroot")
endif()

find_program(CCACHE_PROGRAM ccache)

if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()


if("${PAW_TARGET}" STREQUAL "wasi")
    set(CMAKE_SYSTEM_NAME Generic CACHE STRING "" FORCE)
    set(CMAKE_C_COMPILER clang CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS "--target=wasm32-wasip1 -DWASI" CACHE STRING "" FORCE)
endif()

project(paw C)

option(DOGMAONLY "Only build libdogma, no pawc/pawv" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wno-unused)

if("${PAW_TARGET}" STREQUAL "wasi")
    add_compile_options(-w)
    add_compile_definitions(
        _WASI_EMULATED_PROCESS_CLOCKS
        PK_ENABLE_SOCKET=0
        PK_ENABLE_OS=0
    )
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/nu/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(NOT "${PAW_TARGET}" STREQUAL "wasi")
    include(FetchContent)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_Declare(
        dyncall
        URL https://dyncall.org/r1.4/dyncall-1.4.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(dyncall)

    include_directories(
        ${dyncall_SOURCE_DIR}/dyncall
        ${dyncall_SOURCE_DIR}/dynload
    )
endif()

if("${NEED_DBG}" STREQUAL "yes")
    add_compile_options(-g -O0)
endif()

if("${NEED_ASAN}" STREQUAL "yes")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

add_subdirectory(lib/nu)

find_package(FLEX REQUIRED)
flex_target(Dogmatic src/dogma/lex.l ${CMAKE_CURRENT_BINARY_DIR}/dogma/lex.yy.c)

file(GLOB DOGMA_SRCS "src/dogma/*.c")
list(APPEND DOGMA_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/lson.c")

add_compile_options(-Wall -Wextra -include ${CMAKE_CURRENT_SOURCE_DIR}/include/prel.h)

add_library(dogma_s STATIC ${DOGMA_SRCS} ${FLEX_Dogmatic_OUTPUTS})
set_target_properties(dogma_s PROPERTIES OUTPUT_NAME dogma)
target_link_libraries(dogma_s PRIVATE nu_static)


if(NOT "${PAW_TARGET}" STREQUAL "wasi")
    add_library(dogma_d SHARED ${DOGMA_SRCS} ${FLEX_Dogmatic_OUTPUTS})
    set_target_properties(dogma_d PROPERTIES OUTPUT_NAME dogma)
endif()

if(NOT DOGMAONLY)
	file(GLOB PAWC_SRCS "src/pawc/*.c")

	add_compile_options(-I${CMAKE_CURRENT_SOURCE_DIR}/src/common)

	if("${PAW_TARGET}" STREQUAL "wasi")
		add_executable(pawc.wasm ${PAWC_SRCS})
		target_link_libraries(pawc.wasm PRIVATE nu_static dogma_s)
	endif()

	if(NOT "${PAW_TARGET}" STREQUAL "wasi")
		add_executable(pawc ${PAWC_SRCS})
		target_link_libraries(pawc PRIVATE nu_static dogma_s)
	endif()

	file(GLOB PAWV_SRCS "src/pawv/*.c")

	if("${PAW_TARGET}" STREQUAL "wasi")
		add_executable(pawv.wasm ${PAWV_SRCS})
	endif()

	if(NOT "${PAW_TARGET}" STREQUAL "wasi")
		add_executable(pawv ${PAWV_SRCS})
	endif()

	if(NOT "${PAW_TARGET}" STREQUAL "wasi")
	    target_link_libraries(pawv PRIVATE nu_static dyncall_s dynload_s dogma_s)
	else()
	    target_link_libraries(pawv.wasm PRIVATE nu_static dogma_s)
	endif()
endif()
