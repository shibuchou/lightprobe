cmake_minimum_required(VERSION 3.16)

project(lightprobe C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_subdirectory(controller)
add_subdirectory(runtime)
add_subdirectory(injector)
add_subdirectory(cli)

