# At the moment of writing this gives ~10% speedup of cmake command on Windows
# https://gitlab.kitware.com/cmake/cmake/-/issues/23154
if(POLICY 0115)
  cmake_policy(SET CMP0115 NEW)
endif()

if("${VCPKG_TARGET_TRIPLET}" STREQUAL "")
  if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-sp")
  elseif(APPLE)
    set(VCPKG_TARGET_TRIPLET "x64-osx")
  else()
    set(VCPKG_TARGET_TRIPLET "x64-linux")
  endif()
endif()

set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/overlay_triplets")
set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/overlay_ports")

option(NO_CLEAN_AFTER_BUILD "Don't clean vcpkg temporary folders after build" OFF)

if("$ENV{CI}" STREQUAL "true" OR NO_CLEAN_AFTER_BUILD)
  set(VCPKG_INSTALL_OPTIONS --no-print-usage)
else()
  set(VCPKG_INSTALL_OPTIONS --no-print-usage --clean-after-build)
endif()

if("$ENV{CI}" STREQUAL "true" AND WIN32)
  # The same submodule but moved to a larger disk in Windows CI
  set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
else()
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

# File from vcpkg submodule. This indicates inability to find this file or checkout submodules.
if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  set(msg "${CMAKE_TOOLCHAIN_FILE} doesn't exist. It seems that vcpkg submodule is not initialized.")
  set(msg "${msg}\nUse commands below to initialize:")
  set(msg "${msg}\n  git submodule init")
  set(msg "${msg}\n  git submodule update")
  message(FATAL_ERROR "${msg}")
endif()

if(NOT "${CMAKE_BINARY_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}/build")
  set(msg "CMake binary directory ${CMAKE_BINARY_DIR} must be <repo_root>/build (${CMAKE_SOURCE_DIR}/build)")
  set(msg "${msg}\nUse commands below after cloning the repo:")
  set(msg "${msg}\n  mkdir build")
  set(msg "${msg}\n  cd build")
  set(msg "${msg}\n  cmake ..")
  message(FATAL_ERROR "${msg}")
endif()

cmake_minimum_required(VERSION 3.19)
project(falloutmp)
enable_testing()

# Check the Visual Studio version
if(MSVC)
  if(NOT CMAKE_GENERATOR MATCHES "Visual Studio 17 2022")
    message(FATAL_ERROR "This project requires \"Visual Studio 17 2022\". But the generator was \"${CMAKE_GENERATOR}\"")
  endif()
endif()

# Bool options
option(BUILD_UNIT_TESTS "Build unit tests" ON)
option(OFFLINE_MODE "Enable offline mode in generated server settings" ON)

message(STATUS BUILD_UNIT_TESTS=${BUILD_UNIT_TESTS})
message(STATUS OFFLINE_MODE=${OFFLINE_MODE})

# Path options
option(UNIT_DATA_DIR "Path to a directory with Fallout4.esm, used by the data-dependent tests ([espm], [fo4data]). CMAKE_SOURCE_DIR would be used to resolve a relative path if passed." OFF)
option(FALLOUT4_DIR "Path to Fallout 4 (the folder with Fallout4.exe). Generated server settings point at its Data folder" OFF)
option(CPPCOV_PATH "Path to OpenCppCoverage" OFF)

if(UNIT_DATA_DIR AND NOT IS_ABSOLUTE "${UNIT_DATA_DIR}")
  get_filename_component(UNIT_DATA_DIR "${UNIT_DATA_DIR}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
endif()

string(REPLACE "\\" "/" FALLOUT4_DIR "${FALLOUT4_DIR}")
if(FALLOUT4_DIR)
  if (NOT EXISTS ${FALLOUT4_DIR}/Fallout4.exe)
    message(FATAL_ERROR "Bad FALLOUT4_DIR: ${FALLOUT4_DIR}")
  endif()
endif()

if(NOT CPPCOV_PATH)
  message(STATUS "CPPCOV_PATH is not specified, coverage will not be calculated")
endif()
string(REPLACE "\\" "/" CPPCOV_PATH "${CPPCOV_PATH}")

if(CPPCOV_PATH)
  if (NOT EXISTS ${CPPCOV_PATH}/OpenCppCoverage.exe)
    message(FATAL_ERROR "Bad CPPCOV_PATH: ${CPPCOV_PATH}")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

# Initialize the CMAKEPROJ projects and their configurations
list(APPEND CMAKEPROJ_PROJECTS_LIST)  # List of all projects
set(CMAKEPROJ_PROJECTS_DEPENDENCIES)  # Dependencies for each project
set(CMAKEPROJ_PROJECTS_PRIORITY)      # Priority for each project

# Glob directories under source directory root
file(GLOB ROOT_DIRS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/*)
foreach(dir ${ROOT_DIRS})
  if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/${dir})
    # Check if directory has a cmakeproj.cmake file
    if(EXISTS ${CMAKE_SOURCE_DIR}/${dir}/cmakeproj.cmake)
      include(${CMAKE_SOURCE_DIR}/${dir}/cmakeproj.cmake)
      # Add project to the list if not already added
      if(NOT "${CMAKEPROJ_PROJECTS_LIST}" MATCHES "${dir}")
        list(APPEND CMAKEPROJ_PROJECTS_LIST ${dir})
      endif()
      # Set default priority if not set
      if (NOT DEFINED CMAKEPROJ_PRIORITY_${dir})
        set(CMAKEPROJ_PRIORITY_${dir} 0) # Default priority 0 if not defined
      endif()

      # Store dependencies and priorities for sorting
      set(CMAKEPROJ_PROJECTS_DEPENDENCIES ${CMAKEPROJ_PROJECTS_DEPENDENCIES} "${CMAKEPROJ_DEPS_${dir}}")
      set(CMAKEPROJ_PROJECTS_PRIORITY ${CMAKEPROJ_PROJECTS_PRIORITY} ${CMAKEPROJ_PRIORITY_${dir}})
    endif()
  endif()
endforeach()

foreach(proj ${CMAKEPROJ_PROJECTS_LIST})
  list(APPEND priorities_list ${CMAKEPROJ_PRIORITY_${proj}})  
endforeach()
list(REMOVE_DUPLICATES priorities_list)
list(SORT priorities_list COMPARE NATURAL)

foreach(priority ${priorities_list})
  foreach(proj ${CMAKEPROJ_PROJECTS_LIST})
    if(${CMAKEPROJ_PRIORITY_${proj}} STREQUAL ${priority})
      list(APPEND sorted_projects ${proj})
    endif()
  endforeach()
endforeach()

foreach(project ${sorted_projects})
  message(STATUS "Adding project: ${project} with priority ${CMAKEPROJ_PRIORITY_${project}}")
  add_subdirectory(${project})
endforeach()
