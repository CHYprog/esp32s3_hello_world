
idf_component_register(
    SRCS "main.c" "sh1106.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c
)
