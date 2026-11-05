cmake_minimum_required(VERSION 3.10.0)
project(cegex VERSION 0.1.0 LANGUAGES C)

option(CEGEX_BUILD_TESTS "Build and run the cegex tests" OFF)

function(cegex_configure_target target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
 
    target_compile_options(${target} PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Werror=c23-extensions
        >
    )
endfunction()

set(CEGEX_UTIL_SOURCES
    util/collections.h util/collections.c)

add_executable(cegex main.c ${CEGEX_UTIL_SOURCES})
cegex_configure_target(cegex)

if(CEGEX_BUILD_TESTS)
    enable_testing()
 
    if(CMAKE_VERSION VERSION_LESS 3.12)
        file(GLOB CEGEX_TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/*.c)
    else()
        file(GLOB CEGEX_TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/*.c)
    endif()
 
    set(CEGEX_TEST_TARGETS "")
    foreach(test_source ${CEGEX_TEST_SOURCES})
        get_filename_component(test_name ${test_source} NAME_WE)
        set(test_target cegex_test_${test_name})
 
        add_executable(${test_target} ${test_source} ${CEGEX_UTIL_SOURCES})
        cegex_configure_target(${test_target})
        add_test(NAME ${test_name} COMMAND ${test_target})
 
        list(APPEND CEGEX_TEST_TARGETS ${test_target})
    endforeach()
 
    add_custom_target(run_tests ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
        DEPENDS ${CEGEX_TEST_TARGETS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running cegex tests"
        VERBATIM
    )
endif()