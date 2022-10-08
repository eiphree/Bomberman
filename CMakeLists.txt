cmake_minimum_required (VERSION 3.18)

project(BOMBERMAN_CLIENT)

set(CMAKE_C_STANDARD 20)
set(CMAKE_C_FLAGS "-g -Wall -Wextra -Wconversion -Werror -O2 -std=c++20")

add_executable(robots-client client.cpp magical_consts.h de_serialization.cpp de_serialization.h)
target_link_libraries(robots-client pthread boost_system boost_program_options)
add_executable(robots-server server.cpp magical_consts.h de_serialization.cpp de_serialization.h)
target_link_libraries(robots-server pthread boost_system boost_program_options boost_thread)
