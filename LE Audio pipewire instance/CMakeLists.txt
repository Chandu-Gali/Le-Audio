# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(le_audio_daemon C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gio-2.0)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(BLUETOOTH REQUIRED bluez)
# Optional: liblc3 (define USE_LIBLC3 if found)
pkg_check_modules(LC3 QUIET lc3)
if (LC3_FOUND)
  add_compile_definitions(USE_LIBLC3)
endif()

add_executable(le_audio
  src/main.c
  src/dbus_bap.c
  src/iso_io.c
  src/lc3_codec.c
)

target_include_directories(le_audio PRIVATE
  include
  ${GLIB_INCLUDE_DIRS}
  ${DBUS_INCLUDE_DIRS}
  ${BLUETOOTH_INCLUDE_DIRS}
  ${LC3_INCLUDE_DIRS}
)

target_link_libraries(le_audio
  ${GLIB_LIBRARIES}
  ${DBUS_LIBRARIES}
  ${BLUETOOTH_LIBRARIES}
  ${LC3_LIBRARIES}
)