# RIFT BOARD (S16 赛场看板): the active application. The baseline demo and
# FOG MARCH code stay in the tree for reference and host tests, but only the
# scoreboard is compiled. Application-owned CJK subset fonts are generated
# by tools/gen_rift_fonts.sh (PRD Plan B pattern) and compiled nowhere else.
idf_component_register(
    SRCS "main.c"
         "rift_model.c"
         "rift_champs.c"
         "rift_kb.c"
         "rift_view.c"
         "rift_app.c"
         "rift_blufi.c"
         "rift_blufi_sec.c"
         "rift_net.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_netif esp_wifi bt esp_http_client
             esp-tls lwip mbedtls
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/rift_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/rift_font_20.c"
)
