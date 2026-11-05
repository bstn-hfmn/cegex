cmake_minimum_required(VERSION 3.10.0)
project(cegex VERSION 0.1.0 LANGUAGES C)

add_executable(cegex main.c util/collections.c)
set_target_properties(cegex PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_compile_options(cegex PRIVATE
    $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:
        -Wall 
        -Wextra 
        -Wpedantic 
        -Wconversion
        -Werror=c23-extensions
    >
)