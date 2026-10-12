set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../upstream")
set(SDK_ROOT "${APP_ROOT}/freeink-sdk/libs/hardware")
set(SDFAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.local/dependencies/SdFat")
file(GLOB_RECURSE SDFAT_SRCS "${SDFAT_ROOT}/src/*.cpp")
idf_component_register(
    SRCS ${SDFAT_SRCS}
      "${SDK_ROOT}/SDCardManager/src/SDCardManager.cpp"
      "${SDK_ROOT}/SDCardManager/src/SdmmcBlockDevice.cpp"
      "${SDK_ROOT}/UsbMassStorage/src/UsbMassStorage.cpp"
      "${APP_ROOT}/lib/hal/HalStorage.cpp"
      "${APP_ROOT}/lib/hal/HalMemory.cpp"
      "${APP_ROOT}/lib/Logging/Logging.cpp"
      "${APP_ROOT}/lib/Txt/TxtProgress.cpp"
    INCLUDE_DIRS "include" "${SDFAT_ROOT}/src" "${SDK_ROOT}/SDCardManager/include"
      "${SDK_ROOT}/UsbMassStorage/include" "${APP_ROOT}/lib/hal"
      "${APP_ROOT}/lib/Memory" "${APP_ROOT}/lib/Logging" "${APP_ROOT}/lib/Txt"
    REQUIRES rickyos_platform arduino-esp32-4.0.0-RC1 esp_driver_sdmmc sdmmc
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    FREEINK_SD_SDMMC=1 USE_BLOCK_DEVICE_INTERFACE=1 USE_UTF8_LONG_NAMES=1
    DESTRUCTOR_CLOSES_FILE=1 DISABLE_FS_H_WARNING=1 BOARD_HAS_PSRAM=1 ARDUINO_USB_MODE=1
    ARDUINO_USB_CDC_ON_BOOT=0 LOG_LEVEL=2 ENABLE_SERIAL_LOG=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
