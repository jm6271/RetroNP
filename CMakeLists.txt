cmake_minimum_required(VERSION 3.20)
project(RetroNP VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options("$<$<CONFIG:Release>:/O2>")
else()
  add_compile_options("$<$<CONFIG:Release>:-O2>")
endif()
add_library(retronp_editor STATIC src/editor.cpp)
target_include_directories(retronp_editor PUBLIC src)

# The application and integration tests share the same implementation. The
# executable contributes only startup, never a source file included by tests.
add_library(retronp_app STATIC
  src/app.cpp
  src/dialogs.cpp
  src/dialog_rendering.cpp
  src/document_commands.cpp
  src/file_io.cpp
  src/graphics.cpp
  src/keyboard.cpp
  src/mouse.cpp
  src/presenter.cpp
  src/rendering.cpp
  src/window.cpp
)
target_compile_definitions(retronp_app PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(retronp_app PUBLIC retronp_editor gdi32 user32 shell32)
add_executable(RetroNP WIN32 src/main.cpp)
target_link_libraries(RetroNP PRIVATE retronp_app)
if(MINGW)
  target_link_options(RetroNP PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
foreach(target retronp_editor retronp_app RetroNP)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
add_executable(editor_tests tests/editor_tests.cpp)
target_link_libraries(editor_tests PRIVATE retronp_editor)
enable_testing()
add_test(NAME editor_tests COMMAND editor_tests)
add_executable(io_tests tests/io_tests.cpp)
target_link_libraries(io_tests PRIVATE retronp_app)
if(MINGW)
  target_link_options(io_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME io_tests COMMAND io_tests)
