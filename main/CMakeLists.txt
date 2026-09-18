idf_component_register(
    SRCS
        "main.c"
        "usb_slcan.c"
        "can_twai.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_driver_usb_serial_jtag
        driver
        esp_timer
)