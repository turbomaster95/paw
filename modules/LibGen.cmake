# cmake/AddModShared.cmake
include_guard(GLOBAL)

function(add_mod_shared TARGET_NAME)
    set(options)
    set(one_value_args OUTPUT_NAME)
    set(multi_value_args EXTRA_SRCS INCLUDE_DIRS COMPILE_DEFINITIONS)

    cmake_parse_arguments(
        MOD
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    set(main_source "${CMAKE_CURRENT_SOURCE_DIR}/mod.c")

    if(NOT EXISTS "${main_source}")
        message(FATAL_ERROR
            "add_mod_shared(${TARGET_NAME}): "
            "missing ${main_source}"
        )
    endif()

    set(sources "${main_source}")

    if(NOT MOD_EXTRA_SRCS AND DEFINED EXTRA_SRCS)
        set(MOD_EXTRA_SRCS ${EXTRA_SRCS})
    endif()

    foreach(source IN LISTS MOD_EXTRA_SRCS)
        if(IS_ABSOLUTE "${source}")
            list(APPEND sources "${source}")
        else()
            list(APPEND sources
                "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            )
        endif()
    endforeach()

    add_library("${TARGET_NAME}" SHARED ${sources})

    if(MOD_OUTPUT_NAME)
        set(output_base "${MOD_OUTPUT_NAME}")
    else()
        set(output_base "${TARGET_NAME}")
    endif()

    set_target_properties("${TARGET_NAME}" PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "paw_${output_base}"
        SUFFIX ".so"
        POSITION_INDEPENDENT_CODE ON
    )

    target_include_directories("${TARGET_NAME}"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
            ${MOD_INCLUDE_DIRS}
    )

    if(MOD_COMPILE_DEFINITIONS)
        target_compile_definitions("${TARGET_NAME}"
            PRIVATE ${MOD_COMPILE_DEFINITIONS}
        )
    endif()

    message(STATUS
        "Generating Paw module ${TARGET_NAME}"
    )
endfunction()
