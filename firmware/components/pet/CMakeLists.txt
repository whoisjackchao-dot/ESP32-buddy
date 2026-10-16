# pet 组件：宠物三层（state / render / hal）+ 引擎 + 低层驱动
idf_component_register(
    SRCS "pet_state.c"
         "pet_render.c"
         "pet_text.c"
         "pet_link.c"
         "pet_voice.c"
         "pet_engine.c"
         "pet_hal.c"
         "pet_hal_sticks3.c"
         "pm1.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_timer
    EMBED_FILES "assets/robot.look"
)
