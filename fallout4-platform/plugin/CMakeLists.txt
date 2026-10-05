# FalloutMP game client: the F4SE plugin (FalloutMP.dll).
#
# Standalone project with its own vcpkg manifest, so the Windows build
# doesn't pull the server's dependencies. On other systems it builds the
# portable core and the headless bot only (a check that the source list
# links), since the plugin needs Windows and CommonLibF4.
#
#   cmake -S fallout4-platform/plugin -B build-plugin -G "Visual Studio 17 2022" -A x64 ^
#     -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake ^
#     -DVCPKG_OVERLAY_PORTS=overlay_ports -DVCPKG_TARGET_TRIPLET=x64-windows-static
#   cmake --build build-plugin --config Release --target FalloutMP
# (.github/workflows/falloutmp-client-windows.yml does exactly this)
cmake_minimum_required(VERSION 3.24)
project(falloutmp_plugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /bigobj)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SRV "${REPO}/skymp5-server/cpp")

find_package(spdlog CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(slikenet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(qjs CONFIG REQUIRED)

# The wire codec: the server's message definitions, compiled as they are
add_library(fmp_codec STATIC
  "${SRV}/messages/MessageSerializerFactory.cpp"
  "${SRV}/server_guest_lib/Inventory.cpp"
  "${SRV}/server_guest_lib/Equipment.cpp"
  "${SRV}/server_guest_lib/Appearance.cpp"
  "${SRV}/server_guest_lib/AnimationData.cpp"
  "${SRV}/mp_common/JsonUtils.cpp"
  "${REPO}/serialization/src/BitStreamUtil.cpp")
target_include_directories(fmp_codec PUBLIC
  "${SRV}/messages" "${SRV}/server_guest_lib" "${SRV}/mp_common"
  "${REPO}/serialization/include")
# vcpkg's wrapper names the target SLikeNet; the raw package config doesn't
if(TARGET SLikeNet)
  set(FMP_SLIKENET SLikeNet)
else()
  set(FMP_SLIKENET SLikeNetLibStatic)
endif()
target_link_libraries(fmp_codec PUBLIC
  spdlog::spdlog simdjson::simdjson ${FMP_SLIKENET} nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(fmp_codec PUBLIC ws2_32 iphlpapi)
endif()

file(GLOB core_src "${CMAKE_CURRENT_SOURCE_DIR}/../core/*.cpp")
add_library(fmp_core_plugin STATIC ${core_src})
target_include_directories(fmp_core_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../core")
target_link_libraries(fmp_core_plugin PUBLIC fmp_codec qjs)

if(NOT WIN32)
  add_executable(fmp_bot_standalone "${CMAKE_CURRENT_SOURCE_DIR}/../bot/main.cpp")
  target_link_libraries(fmp_bot_standalone PRIVATE fmp_core_plugin)
  return()
endif()

# CommonLibF4 (libxse), pinned. GPL-3.0 with the modding exceptions: the
# plugin's source is this repository.
include(FetchContent)
set(COMMONLIBF4_COMMIT "7c8c6f8a349ac85abf6fc8a7e5a71732d9339954" CACHE STRING "libxse/commonlibf4 commit")
FetchContent_Declare(commonlibf4
  GIT_REPOSITORY https://github.com/libxse/commonlibf4.git
  GIT_TAG ${COMMONLIBF4_COMMIT}
  GIT_SUBMODULES lib/commonlib-shared
  GIT_SHALLOW FALSE)
# The repository has no CMakeLists.txt, so this only downloads it
FetchContent_MakeAvailable(commonlibf4)
add_subdirectory("${commonlibf4_SOURCE_DIR}/lib/commonlib-shared" commonlib-shared EXCLUDE_FROM_ALL)
file(GLOB_RECURSE commonlibf4_src "${commonlibf4_SOURCE_DIR}/src/*.cpp")
add_library(commonlibf4 STATIC ${commonlibf4_src})
target_include_directories(commonlibf4 PUBLIC "${commonlibf4_SOURCE_DIR}/include")
target_link_libraries(commonlibf4 PUBLIC commonlib-shared::commonlib-shared)
target_precompile_headers(commonlibf4 PRIVATE "${commonlibf4_SOURCE_DIR}/include/F4SE/Impl/PCH.h")

file(GLOB plugin_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
add_library(FalloutMP SHARED ${plugin_src})
target_link_libraries(FalloutMP PRIVATE commonlibf4 fmp_core_plugin)
target_compile_definitions(FalloutMP PRIVATE
  FMP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  FMP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  FMP_VERSION_PATCH=${PROJECT_VERSION_PATCH})
target_precompile_headers(FalloutMP PRIVATE "${commonlibf4_SOURCE_DIR}/include/F4SE/Impl/PCH.h")
