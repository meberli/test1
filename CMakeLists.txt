# ESP-IDF project for the Guition ESP32-4848S040C Loxone panel.
#   idf.py set-target esp32s3   (once)
#   idf.py build flash monitor -p COM14
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(loxone_panel)
