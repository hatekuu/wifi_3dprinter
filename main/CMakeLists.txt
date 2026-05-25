idf_component_register(
    SRCS "sdmmc.c" "main.c" "router.c" "sdspi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server spiffs fatfs sdmmc driver esp_wifi nvs_flash esp_timer
    
)
spiffs_create_partition_image(spiffs spiffs FLASH_IN_PROJECT "${CMAKE_SOURCE_DIR}/spiffs")