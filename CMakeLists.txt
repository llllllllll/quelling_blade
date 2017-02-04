cmake_minimum_required(VERSION 3.10)

project(quelling-blade)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE Debug)
endif()


set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} \
    -Werror \
    -Wall -Wextra \
    -Wsign-compare -Wsuggest-override \
    -Wno-missing-field-initializers \
    -g \
    -Wparentheses -Waggressive-loop-optimizations")

set(CMAKE_CXX_FLAGS_DEBUG
    "-O0 -fmax-errors=5")

set(CMAKE_CXX_FLAGS_SANITIZE
    "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined")

set(CMAKE_CXX_FLAGS_RELEASE
    "-O3 -fstack-protector-strong -flto -march=x86-64 -mtune=native -DNDEBUG")


# Find python interpreter and libraries.
find_package(PythonInterp 3.8 REQUIRED)
find_package(PythonLibs 3.8 REQUIRED)

# Get Python ABI suffix.
exec_program(
  ${PYTHON_EXECUTABLE}
  ARGS "-c \"import sysconfig; print(sysconfig.get_config_var('SOABI'))\""
  OUTPUT_VARIABLE PYTHON_ABI_SUFFIX
  RETURN_VALUE PYTHON_ABI_DETECTION_FAILED
  )

if(PYTHON_ABI_DETECTION_FAILED)
  message(FATAL_ERROR "Failed to detect Python ABI suffix.")
endif()

# Add abseil targets.
add_subdirectory(submodules/abseil-cpp)

# Add target for our extension.
add_library(c_extension SHARED "quelling_blade/arena_allocatable.cc")

set_target_properties(
  c_extension
  PROPERTIES
      PREFIX ""
      OUTPUT_NAME "arena_allocatable.${PYTHON_ABI_SUFFIX}"
      LINKER_LANGUAGES C
      )

target_include_directories(
  c_extension PUBLIC
  ${PYTHON_INCLUDE_DIRS}
  )

target_link_libraries(
  c_extension
  pthread
  ${PYTHON_LIBRARIES}
  absl::hash
  absl::flat_hash_map
  )
