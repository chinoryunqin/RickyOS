idf_component_register(
    SRCS "src/FreeInkDisplay.cpp" "src/bus/EpdBus.cpp" "src/driver/MetalioPlusDriver.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../upstream/lib/hal"
    REQUIRES rickyos_platform metalio_board
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    FREEINK_FB_PSRAM=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1 BOARD_HAS_PSRAM=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
# Diagnostic screen capture over the console (marketing screenshots); off in releases.
if(NOT "$ENV{RICKYOS_PLUS_SCREEN_TOUR}" STREQUAL "")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE RICKYOS_PLUS_SCREEN_DUMP=1)
endif()
