cmake_minimum_required(VERSION 3.10)
project(wl-playground)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(wayland-client wayland-client)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-client-protocol.h
  COMMAND
  wayland-scanner client-header < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-client-protocol.h
  VERBATIM
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-protocol.c
  COMMAND
  wayland-scanner private-code < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-protocol.c
  VERBATIM
)

add_executable(client client.cpp ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-protocol.c)

target_link_libraries(client m)
target_link_libraries(client wayland-client)
