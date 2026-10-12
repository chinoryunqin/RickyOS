file(GLOB BUS_SRCS bus/*.c)
file(GLOB DISPLAY_SRCS display/*.c)
list(FILTER DISPLAY_SRCS EXCLUDE REGEX "/font.c$")
file(GLOB OUTPUT_SRCS display/output_lcd/*.c display/output_common/*.c)
idf_component_register(
 SRCS ${BUS_SRCS} ${DISPLAY_SRCS} ${OUTPUT_SRCS} display/waveforms/my_waveform.c
      metalio_sd.cc SdCardManager.cc portrait_i1.c bus/plus_i2c_wire.cc
 INCLUDE_DIRS . bus display display/output_lcd display/output_common display/waveforms
 REQUIRES rickyos_platform arduino-esp32-4.0.0-RC1 driver esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_driver_dma
          esp_driver_sdmmc esp_lcd esp_hal_lcd esp_hal_rmt esp_hal_i2s esp_adc
          esp_timer esp_psram esp_mm esp_hw_support fatfs sdmmc
)
