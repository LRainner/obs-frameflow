cmake_minimum_required(VERSION 3.28)

# version.txt is owned by release-please, so it is the single source of truth.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" _frameflow_version LIMIT_COUNT 1)
string(STRIP "${_frameflow_version}" _frameflow_version)
if(NOT _frameflow_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "version.txt must hold a numeric semver, got '${_frameflow_version}'")
endif()

project(obs-frameflow VERSION ${_frameflow_version} LANGUAGES C CXX)

set(FRAMEFLOW_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Version string embedded in the build (may be a git tag or CI build id)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FRAMEFLOW_WERROR "Treat compiler warnings as errors" OFF)

# Match the RelWithDebInfo build against a Release libobs install.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel None "")

# libobs is provided by the OBS build / install tree.
find_package(libobs REQUIRED)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/frameflow-version.hpp"
  @ONLY
)

add_library(frameflow MODULE
  src/plugin-main.cpp
  src/frameflow-source.cpp
)

target_include_directories(frameflow PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(frameflow PRIVATE OBS::libobs)

set_target_properties(frameflow PROPERTIES
  PREFIX ""
  OUTPUT_NAME "frameflow"
  FOLDER "plugins"
)

if(MSVC)
  target_compile_options(frameflow PRIVATE /W4 /permissive- /utf-8)
  if(FRAMEFLOW_WERROR)
    target_compile_options(frameflow PRIVATE /WX)
  endif()
else()
  target_compile_options(frameflow PRIVATE -Wall -Wextra)
endif()

if(WIN32)
  set(_frameflow_bin "obs-plugins/64bit")
  set(_frameflow_data "obs-plugins/frameflow")
else()
  include(GNUInstallDirs)
  set(_frameflow_bin "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  set(_frameflow_data "${CMAKE_INSTALL_DATADIR}/obs-plugins/frameflow")
endif()

install(TARGETS frameflow
  RUNTIME DESTINATION "${_frameflow_bin}"
  LIBRARY DESTINATION "${_frameflow_bin}"
)

install(DIRECTORY data/
  DESTINATION "${_frameflow_data}"
)
