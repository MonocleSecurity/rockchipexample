cmake_minimum_required(VERSION 3.21)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(Client C CXX)

find_package(glfw3 CONFIG REQUIRED)

add_executable(rockchipexample
main.hpp
main.cpp)

add_custom_command(TARGET rockchipexample POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/frame.dat ${CMAKE_CURRENT_BINARY_DIR}/frame.dat)

set_property(TARGET rockchipexample PROPERTY CXX_STANDARD 17)

target_link_libraries(rockchipexample EGL)
target_link_libraries(rockchipexample GLESv2)
target_link_libraries(rockchipexample glfw)
target_link_libraries(rockchipexample pthread)
target_link_libraries(rockchipexample rga)
target_link_libraries(rockchipexample /usr/lib/aarch64-linux-gnu/librockchip_mpp.so)
