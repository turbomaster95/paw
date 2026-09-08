cmake_minimum_required(VERSION 3.15)

set(PAW_TARGET "native" CACHE STRING "Target architecture (native|wasi)")

if("${PAW_TARGET}" STREQUAL "wasi")
    set(CMAKE_SYSTEM_NAME Generic CACHE STRING "" FORCE)
    set(CMAKE_C_COMPILER clang CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS "--target=wasm32-wasip1" CACHE STRING "" FORCE)
endif()

project(paw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -pedantic -Wno-unused)

if("${PAW_TARGET}" STREQUAL "wasi")
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

find_package(FLEX REQUIRED)
flex_target(PawLexer src/lex.l ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c)

add_subdirectory(lib/nu)

file(GLOB PAWC_SRCS "src/*.c")
list(REMOVE_ITEM PAWC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/prep.c")

add_executable(pawc
    ${PAWC_SRCS}
    ${FLEX_PawLexer_OUTPUTS}
)
target_link_libraries(pawc PRIVATE nu_static)

file(GLOB PAWV_SRCS "src/vm/*.c")

add_executable(pawv
    ${PAWV_SRCS}
)
target_link_libraries(pawv PRIVATE nu_static)
