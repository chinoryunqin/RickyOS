file(GLOB BUS_SRCS bus/*.c)
file(GLOB DISPLAY_SRCS display/*.c)
list(FILTER DISPLAY_SRCS EXCLUDE REGEX "/font.c$")
file(GLOB OUTPUT_SRCS display/output_lcd/*.c display/output_common/*.c)
idf_component_register(
 SRCS ${BUS_SRCS} ${DISPLAY_SRCS} ${OUTPUT_SRCS} display/waveforms/my_waveform.c
      display/waveforms/my_waveform_trim.c
      metalio_sd.cc SdCardManager.cc portrait_i1.c bus/plus_i2c_wire.cc
 INCLUDE_DIRS . bus display display/output_lcd display/output_common display/waveforms
 REQUIRES rickyos_platform arduino-esp32-4.0.0-RC1 driver esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_driver_dma
          esp_driver_sdmmc esp_lcd esp_hal_lcd esp_hal_rmt esp_hal_i2s esp_adc
          esp_timer esp_psram esp_mm esp_hw_support fatfs sdmmc
)
if("$ENV{RICKYOS_PLUS_GRAY_CALIBRATION}" STREQUAL "1")
 target_compile_definitions(${COMPONENT_LIB} PRIVATE RICKYOS_PLUS_GRAY_CALIBRATION=1)
endif()
if("$ENV{RICKYOS_PLUS_READER_SHORT_WAVEFORM}" STREQUAL "1")
 target_compile_definitions(${COMPONENT_LIB} PRIVATE RICKYOS_PLUS_READER_SHORT_WAVEFORM=1)
endif()
# Phase lookahead is the default (user-accepted 2026-10-20 after a day of device use:
# text turns ~853 -> ~765 ms, UI area updates ~860 -> ~780 ms). RICKYOS_PLUS_PHASE_LOOKAHEAD=0
# builds the legacy cadence for comparison.
if(NOT "$ENV{RICKYOS_PLUS_PHASE_LOOKAHEAD}" STREQUAL "0")
 target_compile_definitions(${COMPONENT_LIB} PRIVATE RICKYOS_PLUS_PHASE_LOOKAHEAD=1)
endif()
