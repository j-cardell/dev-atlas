cmake_minimum_required(VERSION 3.21)
project(dev_atlas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-restrict)
# -Wno-restrict: GCC 12 -O3 false positive on inlined char_traits::copy.

find_package(Threads REQUIRED)

file(GLOB core_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER core_sources EXCLUDE REGEX "main\\.cpp$")
add_library(dev_atlas_core STATIC ${core_sources})
target_include_directories(dev_atlas_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(dev_atlas_core PUBLIC Threads::Threads)

# The CLI and test targets need every component present. Parallel worktrees
# each add their own component files, so these targets only configure once
# scanner, auditor, renderer, and server all exist.
set(components_ok TRUE)
foreach(f src/scanner.cpp src/auditor.cpp src/renderer.cpp src/server.cpp)
  if(NOT EXISTS ${CMAKE_SOURCE_DIR}/${f})
    set(components_ok FALSE)
  endif()
endforeach()

if(components_ok AND EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
  add_executable(dev_atlas ${CMAKE_SOURCE_DIR}/src/main.cpp)
  target_link_libraries(dev_atlas PRIVATE dev_atlas_core)
endif()

enable_testing()
if(components_ok AND EXISTS ${CMAKE_SOURCE_DIR}/tests/test_dev_atlas.cpp)
  add_executable(test_dev_atlas ${CMAKE_SOURCE_DIR}/tests/test_dev_atlas.cpp)
  target_link_libraries(test_dev_atlas PRIVATE dev_atlas_core)
  add_test(NAME dev_atlas COMMAND test_dev_atlas)
endif()
