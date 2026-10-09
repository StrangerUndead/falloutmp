# fallout4-platform: the FalloutMP game client (F4SE plugin).
#
# Inside the repository build (Linux and Windows) this adds the portable
# client core and the headless bot, both covered by unit tests. The F4SE
# plugin itself is built by the standalone project in plugin/ (Windows).
if(EMSCRIPTEN)
  return()
endif()

find_package(qjs CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# fos: Fallout 4 save files for the entrance (F33, the save editor). No
# game headers: the same sources build into the F4SE plugin (plugin/).
file(GLOB fmp_fos_src "${CMAKE_CURRENT_SOURCE_DIR}/fos/*.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/fos/*.h")
add_library(fmp_fos STATIC ${fmp_fos_src})
target_include_directories(fmp_fos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/fos")
target_link_libraries(fmp_fos PUBLIC ZLIB::ZLIB nlohmann_json::nlohmann_json)
apply_default_settings(TARGETS fmp_fos)

add_executable(fmp_savetool "${CMAKE_CURRENT_SOURCE_DIR}/tools/fmp_savetool.cpp")
target_link_libraries(fmp_savetool PRIVATE fmp_fos)
apply_default_settings(TARGETS fmp_savetool)

file(GLOB fmp_core_src "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
                       "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h")
add_library(fmp_core STATIC ${fmp_core_src})
target_include_directories(fmp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
# messages: the wire codec; server_guest_lib: Inventory/Equipment/Appearance
# types the messages embed
# messages also needs server_guest_lib's Inventory/Equipment, so the
# archive is repeated after it for single-pass linkers
target_link_libraries(fmp_core PUBLIC messages server_guest_lib qjs)
if(NOT MSVC)
  target_link_libraries(fmp_core INTERFACE "$<TARGET_FILE:server_guest_lib>")
endif()
apply_default_settings(TARGETS fmp_core)

add_executable(fmp_bot "${CMAKE_CURRENT_SOURCE_DIR}/bot/main.cpp")
target_link_libraries(fmp_bot PRIVATE fmp_core)
apply_default_settings(TARGETS fmp_bot)

add_executable(fmp_make_test_esm "${CMAKE_CURRENT_SOURCE_DIR}/bot/make_test_esm.cpp")
target_include_directories(fmp_make_test_esm PRIVATE "${CMAKE_SOURCE_DIR}/unit")
apply_default_settings(TARGETS fmp_make_test_esm)
