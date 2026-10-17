idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES pet esp_driver_gpio esp_driver_usb_serial_jtag esp_timer nvs_flash)
