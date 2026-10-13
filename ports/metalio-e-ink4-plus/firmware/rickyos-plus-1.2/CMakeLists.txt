cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.2.0-plus-dev")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../.local/arduino-esp32-4.0.0-RC1"
    "${CMAKE_CURRENT_LIST_DIR}/../../.local/dependencies/mdns"
    "${CMAKE_CURRENT_LIST_DIR}/../../.local/usb-dependencies/tinyusb")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(RickyOS-Plus-1.2)

# IDF's S31 HAL zero-initializes register unions; GCC C++ warns about the
# remaining fields. Match the vendor build for this one warning, scoped to
# Arduino rather than disabling errors for the application.
idf_component_get_property(ARDUINO_LIB arduino-esp32-4.0.0-RC1 COMPONENT_LIB)
target_compile_options(${ARDUINO_LIB} PRIVATE -Wno-missing-field-initializers)

idf_component_get_property(MDNS_LIB mdns COMPONENT_LIB)
target_link_libraries(${ARDUINO_LIB} PUBLIC ${MDNS_LIB})

# Preserve the original on-device panic ring capture.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=panic_abort,--wrap=panic_print_backtrace" APPEND)

# IDF adds -fno-lto even when an application opts in. GCC's driver uses
# the presence of that flag to suppress its linker plugin, so remove it
# after project setup while retaining every other IDF link option.
idf_build_get_property(PLUS_LINK_OPTIONS LINK_OPTIONS)
list(REMOVE_ITEM PLUS_LINK_OPTIONS "-fno-lto")
idf_build_set_property(LINK_OPTIONS "${PLUS_LINK_OPTIONS}")
get_target_property(PLUS_ELF_LINK_OPTIONS ${CMAKE_PROJECT_NAME}.elf LINK_OPTIONS)
list(REMOVE_ITEM PLUS_ELF_LINK_OPTIONS "-fno-lto")
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf PROPERTY LINK_OPTIONS "${PLUS_ELF_LINK_OPTIONS}")

# Plus uses Espressif's S31 TinyUSB device wrapper, not Arduino's separate MSC owner.
get_target_property(PLUS_ARDUINO_SOURCES ${ARDUINO_LIB} SOURCES)
list(FILTER PLUS_ARDUINO_SOURCES EXCLUDE REGEX "/cores/esp32/USBMSC\\.cpp$")
set_property(TARGET ${ARDUINO_LIB} PROPERTY SOURCES "${PLUS_ARDUINO_SOURCES}")

# External pinned dependencies live outside PROJECT_DIR. Keep assertion paths
# relative too, preserving source/line diagnostics without workstation prefixes.
get_filename_component(PLUS_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${PLUS_WORKSPACE_ROOT}/=" APPEND)
# LTO optimization is chosen again at final link; keep the application size policy.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE -Oz)
