cmake_minimum_required(VERSION 3.16)
project(spin3d LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(spin3d
    main.cpp
    src/spin3d_common.h
    src/app_config.h
    src/markdown_browser.h
    src/ui_style.h
    src/command_panel.h
    src/settings_panel.h
    src/history_panel.h
    src/holo_core_widget.h
)
target_link_libraries(spin3d Qt6::Widgets Qt6::Network)
