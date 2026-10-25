# pet 组件：宠物三层（state / render / hal）+ 引擎 + 低层驱动
idf_component_register(
    SRCS "pet_state.c"
         "pet_render.c"
         "pet_text.c"
         "pet_link.c"
         "pet_linebuf.c"
         "pet_rx.c"
         "pet_net.c"
         "pet_evt_tcp.c"
         "pet_prov.c"
         "pet_pomo.c"
         "pet_pomo_store.c"
         "pet_button.c"
         "pet_keymap.c"
         "pet_menu.c"
         "pet_leisure.c"
         "pet_keys.c"
         "pet_voice.c"
         "pet_audio.c"
         "pet_ptt.c"
         "pet_engine.c"
         "pet_hal.c"
         "pet_hal_sticks3.c"
         "pm1.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_driver_i2s esp_driver_usb_serial_jtag esp_timer nvs_flash es8311 esp_wifi esp_netif esp_event esp_http_server lwip esp_http_client esp-tls mbedtls
    EMBED_FILES "assets/robot.look"
                "assets/input.pcm"
                "assets/done.pcm"
                "assets/failed.pcm"
)

# 验收用测试构建（Q19=A）：idf.py -DPOMO_FOCUS_MS=30000u -DPOMO_BREAK_MS=10000u
if(DEFINED POMO_FOCUS_MS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "POMO_FOCUS_MS=${POMO_FOCUS_MS}")
endif()
if(DEFINED PET_TEST_CLOCK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "PET_TEST_CLOCK=${PET_TEST_CLOCK}")
endif()
if(DEFINED POMO_BREAK_MS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "POMO_BREAK_MS=${POMO_BREAK_MS}")
endif()
