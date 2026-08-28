cmake_minimum_required(VERSION 3.21)
project(scotty_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 DBus Network)

# The real app lives one directory up; we reuse its QML, icons, theme + update
# checker verbatim, so the demo is pixel-identical to production.
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# update_checker.cc includes the CMake-generated scotty_version.h.
set(SCOTTY_VERSION_TAG "v0.1.0-demo")
set(SCOTTY_UPDATE_REPO "scottydev40/Scotty")
set(SCOTTY_APPIMAGE_ASSET "Scotty-x86_64.AppImage")
configure_file("${APP_DIR}/scotty_version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/scotty_version.h" @ONLY)

qt_add_executable(scotty-demo
  demo_main.cpp
  demo_controller.h
  ${APP_DIR}/theme_controller.cc
  ${APP_DIR}/update_checker.cc
  ${APP_DIR}/version_compare.cc
  ${APP_DIR}/resources_file_share.qrc
)

target_include_directories(scotty-demo PRIVATE
  ${APP_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}  # generated scotty_version.h
)

target_link_libraries(scotty-demo PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Network
)
