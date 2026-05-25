#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_heap_caps.h"

#include "router.h"
#include "sdmmc.h"
#include "main.h"
#include "driver/uart.h"

#define UART_PORT UART_NUM_1
#define TXD_PIN 4
#define RXD_PIN 5
#define BAUDRATE 250000
QueueHandle_t ok_queue;
volatile int64_t last_response_time = 0;
static const char *TAG = "MAIN";
QueueHandle_t upload_queue = NULL;
QueueHandle_t gcode_cmd_queue = NULL;
char uart_log[UART_LOG_MAX][128];
int log_index = 0;
int log_count = 0;
SemaphoreHandle_t log_mutex;
bool is_printing = false;
char wifi_ssid[64] = {0};
char wifi_pass[64] = {0};
void uart_init_printer()
{
    uart_config_t uart_config = {
        .baud_rate = BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    uart_driver_install(UART_PORT, 8192, 8192, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
void uart_log_add(const char *data)
{

    strncpy(uart_log[log_index], data, 127);
    uart_log[log_index][127] = 0;

    log_index = (log_index + 1) % UART_LOG_MAX;

    if (log_count < UART_LOG_MAX)
        log_count++;
}
void uart_rx_task(void *arg)
{
    static char line[256];
    int idx = 0;

    uint8_t buf[64];

    while (1)
    {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));

        for (int i = 0; i < len; i++)
        {
            char c = buf[i];

            if (c == '\n' || c == '\r')
            {
                if (idx == 0)
                    continue;

                line[idx] = 0;

                ESP_LOGI("PRINTER", "<< %s", line);

                // 🔹 OK
                if (strstr(line, "ok"))
                {
                    int dummy = 1;
                    xQueueSend(ok_queue, &dummy, 0);
                    last_response_time = esp_timer_get_time();
                }

                // 🔹 BUSY (🔥 cực quan trọng)
                if (strstr(line, "busy"))
                {
                    last_response_time = esp_timer_get_time();
                }

                // 🔹 nhiệt độ
                if (strstr(line, "T:"))
                {
                    uart_log_add(line);
                }

                // 🔥 lỗi
                if (strstr(line, "error") || strstr(line, "Error"))
                {
                    ESP_LOGE("GCODE", "❌ Printer error!");

                    uart_write_bytes(UART_PORT, "M112\n", 5);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    uart_write_bytes(UART_PORT, "M999\n", 5);

                    is_printing = false;
                }

                idx = 0;
            }
            else
            {
                if (idx < sizeof(line) - 1)
                    line[idx++] = c;
            }
        }

        vTaskDelay(1); // tránh WDT
    }
}
void gcode_stream_task(void *arg)
{
    gcode_cmd_t job;
    // khởi tạo máy in để trả về giá trị nhiệt độ mỗi 2s
    uart_write_bytes(UART_PORT, "M155 S2", 7);
    uart_write_bytes(UART_PORT, "\n", 1);
    while (1)
    {
        if (xQueueReceive(gcode_cmd_queue, &job, portMAX_DELAY))
        {
            // ===== gửi lệnh đơn =====
            if (job.type == CMD_SEND_GCODE)
            {
                uart_write_bytes(UART_PORT, job.data, strlen(job.data));
                uart_write_bytes(UART_PORT, "\n", 1);
                continue;
            }

            // ===== in file =====
            if (job.type == CMD_PRINT_FILE)
            {
                ESP_LOGI("GCODE", "🚀 Start print: %s", job.data);
                is_printing = true;

                FILE *f = fopen(job.data, "r");
                if (!f)
                {
                    ESP_LOGE("GCODE", "❌ Cannot open file");
                    continue;
                }

                char line[256];
                bool error = false;

                while (1)
                {
                    if (!fgets(line, sizeof(line), f))
                        break;

                    // bỏ comment
                    char *comment = strchr(line, ';');
                    if (comment)
                        *comment = '\0';

                    if (strlen(line) < 2)
                        continue;

                    // đảm bảo có newline
                    int l = strlen(line);
                    if (line[l - 1] != '\n')
                    {
                        if (l < sizeof(line) - 1)
                        {
                            line[l] = '\n';
                            line[l + 1] = '\0';
                        }
                    }

                    // 🚀 gửi
                    uart_write_bytes(UART_PORT, line, strlen(line));

                    // ⏳ chờ ok (nhưng không chết nếu busy)
                    while (1)
                    {
                        int ack;
                        if (xQueueReceive(ok_queue, &ack, pdMS_TO_TICKS(100)))
                        {
                            break; // có ok → gửi tiếp
                        }

                        int64_t now = esp_timer_get_time();

                        // 🔥 timeout thật sự (không có ok + không có busy)
                        if ((now - last_response_time) > 5 * 1000000) // 5s
                        {
                            ESP_LOGE("GCODE", "❌ Timeout waiting response");
                            error = true;
                            break;
                        }
                    }

                    if (error)
                        break;

                    vTaskDelay(1); // giảm spam
                }

                fclose(f);
                is_printing = false;

                if (error)
                    ESP_LOGE("GCODE", "❌ Print failed");
                else
                    ESP_LOGI("GCODE", "✅ Done print");
            }
        }

        vTaskDelay(1);
    }
}
void sd_task(void *arg)
{
    upload_chunk_t chunk;
    FILE *f = NULL;
    char current_path[128] = {0};

    static uint8_t sd_buf[SD_BUF_SIZE];
    size_t sd_len = 0;

    while (1)
    {
        if (xQueueReceive(upload_queue, &chunk, portMAX_DELAY))
        {
            // ===== đổi file =====
            if (f && strcmp(current_path, chunk.filepath) != 0)
            {
                if (sd_len > 0)
                {
                    fwrite(sd_buf, 1, sd_len, f);
                    sd_len = 0;
                }

                fclose(f);
                f = NULL;
            }

            // ===== mở file =====
            if (!f)
            {
                strncpy(current_path, chunk.filepath, sizeof(current_path) - 1);

                f = fopen(chunk.filepath, "wb");
                if (!f)
                {
                    ESP_LOGE("SD", "❌ fopen fail: %s", chunk.filepath);
                    continue;
                }

                // 🔥 pre-allocate (QUAN TRỌNG)
                if (chunk.total_size > 0)
                {
                    fseek(f, chunk.total_size - 1, SEEK_SET);
                    fputc(0, f);
                    fflush(f);

                    fseek(f, 0, SEEK_SET);
                }

                ESP_LOGI("SD", "📂 Open: %s (%d bytes)", current_path, chunk.total_size);
            }

            // ===== copy vào buffer =====
            memcpy(sd_buf + sd_len, chunk.data, chunk.len);
            sd_len += chunk.len;
            free(chunk.data);
            // ===== ghi aligned block =====
            size_t aligned = (sd_len / BLOCK_SIZE) * BLOCK_SIZE;

            if (aligned >= CHUNK_SIZE) // chỉ ghi khi đủ lớn
            {
                fwrite(sd_buf, 1, aligned, f);

                // giữ lại phần dư
                memmove(sd_buf, sd_buf + aligned, sd_len - aligned);
                sd_len -= aligned;
            }

            // ===== chunk cuối =====
            if (chunk.is_last)
            {
                if (sd_len > 0)
                {
                    fwrite(sd_buf, 1, sd_len, f);
                    sd_len = 0;
                }

                fflush(f);
                fclose(f);
                f = NULL;

                ESP_LOGI("SD", "✅ Done: %s", current_path);
            }
        }
    }
}

void app_main(void)
{
    uart_init_printer();

    ESP_LOGI(TAG, "Init NVS...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Init SD...");
    sdmmc_init();
    

    ok_queue = xQueueCreate(10, sizeof(int));
    last_response_time = esp_timer_get_time();
    gcode_cmd_queue = xQueueCreate(3, sizeof(gcode_cmd_t));
    // 🔥 QUAN TRỌNG: tạo queue trước
    upload_queue = xQueueCreate(QUEUE_LEN, sizeof(upload_chunk_t));
    log_mutex = xSemaphoreCreateMutex();
    if (gcode_cmd_queue == NULL)
    {
        ESP_LOGE(TAG, "❌ Failed to create queue!");
        abort(); // hoặc return
    }
    if (log_mutex == NULL)
    {
        ESP_LOGE(TAG, "❌ Failed to create mutex!");
        abort(); // hoặc return
    }
    if (upload_queue == NULL)
    {
        ESP_LOGE(TAG, "❌ Failed to create queue!");
        abort(); // hoặc return
    }
    xTaskCreatePinnedToCore(
        sd_task,
        "sd_task",
        8192,
        NULL,
        10, // priority cao hơn HTTP
        NULL,
        1 // chạy core 1
    );
    xTaskCreatePinnedToCore(
        gcode_stream_task,
        "gcode_stream",
        8192,
        NULL,
        9,
        NULL,
        1);
    xTaskCreatePinnedToCore(
        uart_rx_task,
        "uart_rx",
        4096,
        NULL,
        8,
        NULL,
        1);
    ESP_LOGI(TAG, "Init WiFi...");
    wifi_init();

    ESP_LOGI(TAG, "Init SPIFFS...");
    init_spiffs();
    ESP_LOGI(TAG, "Start Web Server...");
    start_webserver();
    ESP_LOGI("RAM", "Internal RAM free: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI("RAM", "PSRAM free: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "System ready 🚀");
}
