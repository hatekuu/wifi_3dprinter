#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "SDMMC";

void sdmmc_init(void)
{
    esp_err_t ret;

    // Cấu hình mount FAT
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    sdmmc_card_t *card;

    // SDMMC host mặc định
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 20000; // giảm xuống cho ổn định
    // ⚠️ Quan trọng: chọn bus width
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = 36;
    slot_config.cmd = 37;
    slot_config.d0 = 38;
    slot_config.d1 = 39;
    slot_config.d2 = 40;
    slot_config.d3 = 41;
    // Nếu bạn chỉ nối D0 → dùng 1-bit
    // slot_config.width = 1;

    // Nếu nối đủ D0-D3 → dùng 4-bit
    slot_config.width = 4;

    // Pull-up nội (khuyến nghị vẫn dùng điện trở ngoài)
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem...");

    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SD card mounted");

    // In thông tin thẻ
    sdmmc_card_print_info(stdout, card);

    // Test ghi file
    FILE *f = fopen("/sdcard/hello.txt", "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello SDMMC!\n");
    fclose(f);

    ESP_LOGI(TAG, "File written");

    // Test đọc file
    f = fopen("/sdcard/hello.txt", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);

    ESP_LOGI(TAG, "Read from file: %s", line);

    // Unmount nếu cần
    // esp_vfs_fat_sdcard_unmount("/sdcard", card);
}
