#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_err.h"

// ================= CONFIG =================

#define UART_LOG_MAX 100

#define SD_BUF_SIZE  (64 * 1024)
#define CHUNK_SIZE   (32 * 1024)
#define BLOCK_SIZE   512
#define QUEUE_LEN    8

#define MOUNT_POINT "/sdcard"

// ================= WIFI =================

#define WIFI_SSID "YOUR_WIFI"
#define WIFI_PASS "YOUR_PASS"

// ================= UART =================

#define UART_PORT UART_NUM_1
#define TXD_PIN   4
#define RXD_PIN   5
#define BAUDRATE  250000

// ================= STRUCT =================

typedef enum
{
    CMD_SEND_GCODE,
    CMD_PRINT_FILE

} gcode_cmd_type_t;

typedef struct
{
    gcode_cmd_type_t type;
    char data[256];

} gcode_cmd_t;

typedef struct
{
    uint8_t *data;

    size_t len;
    int total_size;

    bool is_first;
    bool is_last;

    char filepath[128];

} upload_chunk_t;

typedef struct
{
    char filepath[128];

} print_job_t;

// ================= GLOBAL =================

extern QueueHandle_t upload_queue;
extern QueueHandle_t gcode_cmd_queue;
extern QueueHandle_t ok_queue;

extern SemaphoreHandle_t log_mutex;

extern volatile int64_t last_response_time;

extern bool is_printing;

extern char uart_log[UART_LOG_MAX][128];
extern int log_index;
extern int log_count;

extern char wifi_ssid[64];
extern char wifi_pass[64];

#endif