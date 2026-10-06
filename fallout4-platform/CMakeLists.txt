# fallout4-platform: the FalloutMP game client (F4SE plugin).
#
# Inside the repository build (Linux and Windows) this adds the portable
# client core and the headless bot, both covered by unit tests. The F4SE
# plugin itself is built by the standalone project in plugin/ (Windows).
if(EMSCRIPTEN)
  return()
endif()

find_package(qjs CONFIG REQUIRED)

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
