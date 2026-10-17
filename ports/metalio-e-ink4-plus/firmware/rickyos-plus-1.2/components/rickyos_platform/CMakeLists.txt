idf_component_register(INCLUDE_DIRS "include" "../../upstream/freeink-sdk/libs/hardware/BoardConfig/include"
    REQUIRES arduino-esp32-4.0.0-RC1 esp_driver_gpio mbedtls efuse esp_netif esp_event esp_wifi
        esp_driver_uart esp_driver_i2c esp_driver_ledc esp_driver_usb_serial_jtag)
# RICKYOS_GRAY_PRESET 2 is the Plus panel tone (ink2 2, mute 5, line 7, tint 12; fills 7/4; 3 px hairline).
target_compile_definitions(${COMPONENT_LIB} INTERFACE FREEINK_DEVICE_METALIO_PLUS=1 FREEINK_CAP_USB_MSC=1
    RICKYOS_GRAY_PRESET=2)
