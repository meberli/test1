idf_component_register(
    SRCS "app_main.cpp" "sys.cpp" "net.cpp" "main.cpp" "panel_hw.cpp" "panel_ui.cpp"
         "loxone_client.cpp" "loxone_structure.cpp" "test_harness.cpp" "weather_derive.cpp"
         "font_dejavu_14.c" "font_dejavu_20.c" "font_icons_44.c" "font_icons_24.c" "settings.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls esp_lcd esp_driver_gpio
             esp_driver_i2c esp_driver_uart esp_timer esp_psram spi_flash esp_mm mbedtls ArduinoJson)
# The fonts and LVGL use designated initialisers in C++ headers.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-unused-variable
                       -Wno-format)
