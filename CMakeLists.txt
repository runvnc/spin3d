cmake_minimum_required(VERSION 3.16)
project(spin3d LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)

add_executable(spin3d main.cpp)
target_link_libraries(spin3d Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network)
