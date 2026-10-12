# ESP-IDF project for the Guition ESP32-4848S040C Loxone panel.
#   idf.py set-target esp32s3   (once)
#   idf.py build flash monitor -p COM14
cmake_minimum_required(VERSION 3.22)
# Local secrets (Windy API key) live in the gitignored sdkconfig.defaults.local
# and are merged after the regular defaults (and after sdkconfig.test when the
# test build passes -D SDKCONFIG_DEFAULTS=...).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
  if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
  endif()
  set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};sdkconfig.defaults.local")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(loxone_panel)
