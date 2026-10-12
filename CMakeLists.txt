cmake_minimum_required(VERSION 3.16)
project(PH3039 NONE)

set(FQBN "arduino:avr:uno" CACHE STRING "Board FQBN")
set(PORT "" CACHE STRING "Serial port (the Makefile auto-detects it)")
set(BAUD 9600 CACHE STRING "Monitor baud rate")
set(SKETCH_ENTRY "main.cpp" CACHE STRING "Main sketch file in src/")

find_program(ARDUINO_CLI arduino-cli REQUIRED)

set(SKETCH_DIR ${CMAKE_BINARY_DIR}/sketch)
set(SKETCH_INO ${SKETCH_DIR}/sketch.ino)

# Stage only the necessary files to avoid "multiple definition of setup/loop" errors
add_custom_command(
    OUTPUT ${SKETCH_INO}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SKETCH_DIR}
    # Copy all header files (e.g., fmt.h)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/src/*.h ${SKETCH_DIR}/
    # Copy ONLY the selected entry point and rename it to sketch.ino
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/${SKETCH_ENTRY} ${SKETCH_INO}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/${SKETCH_ENTRY}
)

add_custom_target(build ALL
    COMMAND ${ARDUINO_CLI} compile --fqbn ${FQBN} --build-path ${CMAKE_BINARY_DIR}/out ${SKETCH_DIR}
    DEPENDS ${SKETCH_INO}
    USES_TERMINAL
)

add_custom_target(upload
    COMMAND ${ARDUINO_CLI} compile --fqbn ${FQBN} --build-path ${CMAKE_BINARY_DIR}/out --upload --port ${PORT} ${SKETCH_DIR}
    DEPENDS ${SKETCH_INO}
    USES_TERMINAL
)

add_custom_target(monitor
    COMMAND ${ARDUINO_CLI} monitor --port ${PORT} --config baudrate=${BAUD}
    USES_TERMINAL
)
