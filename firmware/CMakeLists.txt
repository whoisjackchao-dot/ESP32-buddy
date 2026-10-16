cmake_minimum_required(VERSION 3.16)

# ESP32-buddy 固件（M1：在 M5StickS3 上显示宠物一帧）
# 分层：components/pet 下的 pet_state / pet_render / pet_engine / pet_hal_sticks3
# 低层 ST7789 与 M5PM1 驱动复用同作者既有 StickS3 项目（MIT）的已验证实现。
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_buddy)
