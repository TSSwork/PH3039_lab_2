cmake_minimum_required(VERSION 3.16)
project(daq NONE)

set(FQBN "arduino:avr:uno" CACHE STRING "Board FQBN")
set(LIBS "" CACHE STRING "Arduino libraries to install (semicolon separated)")

find_program(ARDUINO_CLI arduino-cli REQUIRED)

set(SKETCH_DIR ${CMAKE_BINARY_DIR}/sketch)
set(SKETCH_INO ${SKETCH_DIR}/sketch.ino)
set(OUT_DIR    ${CMAKE_BINARY_DIR}/out)
set(LIBS_STAMP ${CMAKE_BINARY_DIR}/libs.stamp)
file(GLOB SRC_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*)

# Install Arduino libraries once; re-runs only when project.yaml changes.
if(LIBS)
  set(LIB_CMD COMMAND ${ARDUINO_CLI} lib install ${LIBS})
endif()
add_custom_command(
  OUTPUT ${LIBS_STAMP}
  ${LIB_CMD}
  COMMAND ${CMAKE_COMMAND} -E touch ${LIBS_STAMP}
  DEPENDS ${CMAKE_SOURCE_DIR}/project.yaml
  VERBATIM
)
add_custom_target(libs DEPENDS ${LIBS_STAMP})

# arduino-cli wants <name>/<name>.ino: stage src/ as a sketch.
add_custom_command(
  OUTPUT ${SKETCH_INO}
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/stage-sketch.sh ${CMAKE_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS ${SRC_FILES}
  VERBATIM
)

add_custom_target(build ALL
  COMMAND ${ARDUINO_CLI} compile --fqbn ${FQBN} --build-path ${OUT_DIR} ${SKETCH_DIR}
  DEPENDS ${SKETCH_INO} ${LIBS_STAMP}
  USES_TERMINAL
)

# PORT comes from the environment (exported by the Makefile).
add_custom_target(upload
  COMMAND sh -c "${ARDUINO_CLI} upload --fqbn ${FQBN} --port \"$PORT\" --input-dir ${OUT_DIR} ${SKETCH_DIR}"
  DEPENDS build
  USES_TERMINAL
  VERBATIM
)
