include(${CMAKE_SOURCE_DIR}/cmake/yarn.cmake)

file(GLOB_RECURSE sources ${CMAKE_CURRENT_LIST_DIR}/*.ts)

set(out
  ${CMAKE_BINARY_DIR}/dist/server/dist_back/falloutmp-server.js
)

if(WIN32)
  add_custom_command(
    OUTPUT ${out}
    COMMAND yarn --cwd "\"${FALLOUTMP_SERVER_SOURCE_DIR}\"" build-ts
    DEPENDS ${sources}
  )
else()
  add_custom_command(
    OUTPUT ${out}
    COMMAND yarn
    COMMAND yarn build-ts
    WORKING_DIRECTORY ${FALLOUTMP_SERVER_SOURCE_DIR}
    DEPENDS ${sources}
  )
endif()

add_custom_target(falloutmp-server-ts ALL
  DEPENDS ${out}
  SOURCES ${sources}
)
