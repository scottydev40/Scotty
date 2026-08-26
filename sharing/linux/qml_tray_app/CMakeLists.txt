cmake_minimum_required(VERSION 3.21)
project(scotty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 DBus Network)
include(GNUInstallDirs)

# --- Version stamp for the in-app update checker ---------------------------
# Resolve this build's release tag from git (falls back to the project version
# for source-tarball builds with no git). The update checker compares it against
# GitHub release tags.
find_package(Git QUIET)
set(SCOTTY_VERSION_TAG "v${PROJECT_VERSION}")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _scotty_git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_scotty_git_describe)
    set(SCOTTY_VERSION_TAG "${_scotty_git_describe}")
  endif()
endif()
set(SCOTTY_UPDATE_REPO "scottydev40/Scotty")
set(SCOTTY_APPIMAGE_ASSET "Scotty-x86_64.AppImage")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/scotty_version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/scotty_version.h"
  @ONLY)

# Where the Bazel-built Nearby .so and header were installed
set(NEARBY_PREFIX "/usr/local" CACHE PATH "Install prefix for the Nearby shared library")

find_library(NEARBY_SHARING_LIB nearby_sharing_api_shared
  HINTS "${NEARBY_PREFIX}/lib" REQUIRED)

find_path(NEARBY_SHARING_INCLUDE sharing/linux/nearby_sharing_api.h
  HINTS "${NEARBY_PREFIX}/include" REQUIRED)

find_library(QRENCODE_LIB
  NAMES qrencode libqrencode.so.4
  HINTS /usr/lib64 /usr/lib /usr/local/lib
  REQUIRED)

qt_add_executable(nearby_qml_file_tray_app
  file_share_tray_main.cpp
  file_share_tray_controller.cc
  file_share_tray_controller.h
  file_share_state.cc
  file_share_state.h
  app_paths.cc
  app_paths.h
  theme_controller.cc
  theme_controller.h
  update_checker.cc
  update_checker.h
  version_compare.cc
  version_compare.h
  bluetooth_name_guard.cc
  bluetooth_name_guard.h
  quick_share_dbus.cc
  quick_share_dbus.h
  freedesktop_application_adaptor.cc
  freedesktop_application_adaptor.h
  share_target_model.h
  transfer_model.h
  string_utils.cc
  string_utils.h
  status_mapper.cc
  status_mapper.h
  qr_code_generator.cc
  qr_code_generator.h
  notification_manager.cpp
  notification_manager.h
  third_party/libqrencode/qrencode_compat.h
  resources_file_share.qrc
)

target_include_directories(nearby_qml_file_tray_app PRIVATE
  "${NEARBY_SHARING_INCLUDE}"
  "${CMAKE_CURRENT_BINARY_DIR}"  # generated scotty_version.h
)

target_link_libraries(nearby_qml_file_tray_app PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Network
  "${QRENCODE_LIB}"
  "${NEARBY_SHARING_LIB}"
)

# --- Unit tests (comparator is pure std C++; no Qt/Nearby needed) ----------
# Build with -DBUILD_TESTING=OFF to skip (e.g. minimal AppImage build hosts).
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_executable(version_compare_test
    version_compare_test.cc
    version_compare.cc
    version_compare.h)
  add_test(NAME version_compare_test COMMAND version_compare_test)
endif()

set_target_properties(nearby_qml_file_tray_app PROPERTIES
  OUTPUT_NAME scotty
  INSTALL_RPATH "$ORIGIN/../lib/scotty"
)

set(SCOTTY_APP_ID "dev.scotty.Scotty")
set(SCOTTY_EXTENSION_UUID "quickshare@scottydev40.github.io")
set(SCOTTY_PACKAGING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packaging")
set(SCOTTY_EXTENSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../quickshare-gnome-extension")

configure_file(
  "${SCOTTY_PACKAGING_DIR}/${SCOTTY_APP_ID}.service.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${SCOTTY_APP_ID}.service"
  @ONLY
)
configure_file(
  "${SCOTTY_PACKAGING_DIR}/scotty.service.in"
  "${CMAKE_CURRENT_BINARY_DIR}/scotty.service"
  @ONLY
)

install(TARGETS nearby_qml_file_tray_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${NEARBY_SHARING_LIB}" DESTINATION "lib/scotty")
install(FILES
  "${SCOTTY_PACKAGING_DIR}/${SCOTTY_APP_ID}.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES
  "${SCOTTY_PACKAGING_DIR}/${SCOTTY_APP_ID}.metainfo.xml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
  RENAME "${SCOTTY_APP_ID}.png"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
  RENAME "${SCOTTY_APP_ID}.svg"
)
install(FILES
  "${SCOTTY_EXTENSION_DIR}/${SCOTTY_APP_ID}-symbolic.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/${SCOTTY_APP_ID}.service"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/scotty.service"
  DESTINATION "lib/systemd/user"
)
