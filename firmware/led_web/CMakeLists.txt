cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Version affichée dans Home Assistant (appareil → version du firmware)
set(PROJECT_VER "2.0.0")

project(led_web)
