# TODO: enable for Emscripten once stabilize
if(BUILD_UNIT_TESTS AND NOT EMSCRIPTEN)

  include(${CMAKE_SOURCE_DIR}/cmake/apply_default_settings.cmake)

  #
  # unit executable
  #

  file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*")
  list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")

  add_executable(unit ${src})

  find_package(Catch2 CONFIG REQUIRED)
  target_link_libraries(unit PRIVATE Catch2::Catch2)

  target_link_libraries(unit PUBLIC server_guest_lib espm)
  if(TARGET fmp_core)
    target_link_libraries(unit PUBLIC fmp_core)
    target_compile_definitions(unit PRIVATE WITH_FMP_CORE=1)
  endif()
  apply_default_settings(TARGETS unit)
  list(APPEND VCPKG_DEPENDENT unit)

  target_compile_definitions(unit PRIVATE
    TEST_PEX_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/papyrus_test_files/standard_scripts\"
    BUILT_PEX_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/papyrus_test_files/pex\"
    UNIT_DATA_DIR=\"${UNIT_DATA_DIR}\"
    DIST_DIR=\"${CMAKE_BINARY_DIR}/dist\"
  )

  if(WIN32)
    target_compile_options(unit PRIVATE "/bigobj")
    target_link_libraries(unit PUBLIC Dbghelp.lib)
  endif()

  #
  # ctest tests
  #

  # run without coverage but with dumps
  add_test(
    NAME test_unit
    COMMAND ${CMAKE_COMMAND}
      -DEXE_PATH=$<TARGET_FILE:unit>
      -DCOVERAGE_HTML_OUT_DIR=${CMAKE_BINARY_DIR}/__coverage
      -DCPPCOV=OFF
      -DCPPCOV_PATH=${CPPCOV_PATH}
      -DUNIT_WORKING_DIRECTORY=${CMAKE_BINARY_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/run_test_unit.cmake
  )
  set_tests_properties(test_unit PROPERTIES
    FIXTURES_SETUP unit_passed
  )

  # run with coverage but without dumps
  if(CPPCOV_PATH)
    add_test(
      NAME test_unit_coverage
      COMMAND ${CMAKE_COMMAND}
        -DEXE_PATH=$<TARGET_FILE:unit>
        -DCOVERAGE_HTML_OUT_DIR=${CMAKE_BINARY_DIR}/__coverage
        -DCPPCOV=ON
        -DCPPCOV_PATH=${CPPCOV_PATH}
        -P ${CMAKE_SOURCE_DIR}/cmake/run_test_unit.cmake
    )
  endif()
endif()
