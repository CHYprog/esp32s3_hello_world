
idf_component_register(
    SRCS "main.c" "sh1106.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c
)

idf_build_get_property(python PYTHON)
set(font_bdf "${CMAKE_CURRENT_LIST_DIR}/../fonts/wenquanyi/wenquanyi_12pt.bdf")
set(font_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_font_zh16.py")
set(font_data "${CMAKE_CURRENT_BINARY_DIR}/font_zh16.inc")

add_custom_command(
    OUTPUT "${font_data}"
    COMMAND "${python}" "${font_generator}" "${font_bdf}" "${font_data}"
    DEPENDS "${font_bdf}" "${font_generator}"
    COMMENT "Generating 16-pixel OLED font from WenQuanYi BDF"
    VERBATIM
)

target_sources(${COMPONENT_LIB} PRIVATE "${font_data}")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
