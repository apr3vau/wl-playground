cmake_minimum_required(VERSION 3.10)
project(wl-playground)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(xdg-shell /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
set(client-protocol ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-client-protocol.h)
set(protocol ${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-protocol.c)

find_library(wayland-client wayland-client)

add_custom_command(
  OUTPUT ${protocol} ${client-protocol}
  COMMAND
  wayland-scanner private-code < ${xdg-shell} > ${protocol} &&
  wayland-scanner client-header < ${xdg-shell} > ${client-protocol}
  VERBATIM
)

add_executable(client client.cpp ${protocol})

target_link_libraries(client m)
target_link_libraries(client wayland-client)
