
#include "router.h"
#include <ctype.h>
#include "main.h"
static const char *TAG = "ROUTER";
#include <errno.h>

//================= WIFI =================
// void wifi_init(void)
// {
//     esp_netif_init();
//     esp_event_loop_create_default();

//     esp_netif_create_default_wifi_ap();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     esp_wifi_init(&cfg);

//     wifi_config_t wifi_config = {
//         .ap = {
//             .ssid = "ESP32_UPLOAD",
//             .ssid_len = strlen("ESP32_UPLOAD"),
//             .password = "12345678",
//             .max_connection = 4,
//             .authmode = WIFI_AUTH_WPA_WPA2_PSK
//         },
//     };

//     if (strlen((char *)wifi_config.ap.password) == 0)
//     {
//         wifi_config.ap.authmode = WIFI_AUTH_OPEN;
//     }

//     esp_wifi_set_mode(WIFI_MODE_AP);
//     esp_wifi_set_config(WIFI_IF_AP, &wifi_config);

//     // 🔥 QUAN TRỌNG: tắt power save
//     esp_wifi_set_ps(WIFI_PS_NONE);

//     esp_wifi_start();

//     ESP_LOGI("WIFI", "AP started. SSID: %s", wifi_config.ap.ssid);
// }
void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS},
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}
// ================= SPIFFS =================
void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true};

    esp_vfs_spiffs_register(&conf);
}

// ================= FILE HANDLER =================
esp_err_t file_get_handler(httpd_req_t *req)
{
    char filepath[128];
    // ❗ tránh nuốt API
    if (strcmp(req->uri, "/list") == 0 ||
        strcmp(req->uri, "/upload") == 0 ||
        strcmp(req->uri, "/delete") == 0)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    if (strcmp(req->uri, "/") == 0)
    {
        strcpy(filepath, "/spiffs/index.html");
    }
    else
    {
        snprintf(filepath, sizeof(filepath), "/spiffs%.100s", req->uri);
    }

    ESP_LOGI(TAG, "Request: %s", filepath);

    FILE *f = fopen(filepath, "r");
    if (!f)
    {

        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Content type
    if (strstr(req->uri, ".css"))
    {
        httpd_resp_set_type(req, "text/css");
    }
    else if (strstr(req->uri, ".js"))
    {
        httpd_resp_set_type(req, "application/javascript");
    }
    else
    {
        httpd_resp_set_type(req, "text/html");
    }

    char chunk[512];
    size_t read_bytes;

    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}
// ================= FILE HANDLER =================
esp_err_t list_handler(httpd_req_t *req)
{
    DIR *dir = opendir("/sdcard");
    if (!dir)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr_chunk(req, "[");

    struct dirent *entry;
    int first = 1;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!first)
            httpd_resp_sendstr_chunk(req, ",");
        first = 0;

        char line[128];
        snprintf(line, sizeof(line), "{\"name\":\"%.100s\"}", entry->d_name);
        httpd_resp_sendstr_chunk(req, line);
    }

    closedir(dir);

    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}
void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b)))
        {

            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a = a - 'A' + 10;
            else
                a -= '0';

            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b = b - 'A' + 10;
            else
                b -= '0';

            *dst++ = 16 * a + b;
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
// ================= UPLOAD HANDLER =================
esp_err_t upload_handler(httpd_req_t *req)
{
    char filepath[128];
    char filename[64] = "upload.bin";

    int total_size = req->content_len;
    int remaining = total_size;

    // ===== Lấy filename từ query =====
    char query[128];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char raw[64] = {0};

        if (httpd_query_key_value(query, "name", raw, sizeof(raw)) == ESP_OK)
        {
            url_decode(filename, raw);
        }
    }

    // ===== sanitize filename =====
    for (int i = 0; filename[i]; i++)
    {
        if (strchr("/\\:*?\"<>|", filename[i]))
        {
            filename[i] = '_';
        }
    }

    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);

    ESP_LOGI("UPLOAD", "📥 %s (%d bytes)", filepath, total_size);

    // ===== receive file chunks =====
    while (remaining > 0)
    {
        int to_read = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

        upload_chunk_t chunk = {0};

        chunk.data = heap_caps_malloc(to_read, MALLOC_CAP_SPIRAM);
        if (!chunk.data)
        {
            ESP_LOGE("UPLOAD", "malloc fail");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        int recv_len = httpd_req_recv(req, (char *)chunk.data, to_read);

        if (recv_len <= 0)
        {
            free(chunk.data);
            ESP_LOGE("UPLOAD", "recv failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        chunk.len = recv_len;
        chunk.total_size = total_size;
        chunk.is_last = (remaining - recv_len == 0);
        chunk.is_first = (remaining == total_size);

        strncpy(chunk.filepath, filepath, sizeof(chunk.filepath) - 1);

        while (xQueueSend(upload_queue, &chunk, pdMS_TO_TICKS(10)) != pdTRUE)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        remaining -= recv_len;
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
// dowload handler
esp_err_t download_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", filename, sizeof(filename)) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);

    FILE *f = fopen(filepath, "rb");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // 🔥 QUAN TRỌNG NHẤT: force download
    char header[200];
    snprintf(header, sizeof(header),
             "attachment; filename=\"%s\"",
             filename);

    httpd_resp_set_hdr(req, "Content-Disposition", header);

    httpd_resp_set_type(req, "application/octet-stream");

    char buf[1024];
    size_t read_bytes;

    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read_bytes);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}
// ================= DELETE HANDLER =================
esp_err_t delete_handler(httpd_req_t *req)
{
    char filename[64];

    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", filename, sizeof(filename)) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    if (remove(filepath) == 0)
    {
        httpd_resp_sendstr(req, "Deleted");
    }
    else
    {
        httpd_resp_send_500(req);
    }

    return ESP_OK;
}

//===================== PRINT HANDLER =================
esp_err_t print_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", filename, sizeof(filename)) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    print_job_t job;
    snprintf(job.filepath, sizeof(job.filepath), "/sdcard/%s", filename);

    // gửi vào queue
    if (xQueueSend(gcode_cmd_queue, &job,
                   pdMS_TO_TICKS(100)) != pdTRUE)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI("PRINT", "📥 Queue print: %s", job.filepath);

    httpd_resp_sendstr(req, "Printing started");
    return ESP_OK;
}
// ================= SERVER =================
void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.uri_match_fn = httpd_uri_match_wildcard;
    // 👇 THÊM MẤY DÒNG NÀY
    config.recv_wait_timeout = 10; // timeout nhận data (giây)
    config.send_wait_timeout = 10;
    config.max_uri_handlers = 16;
    config.stack_size = 16384; // tăng stack (quan trọng)
    httpd_start(&server, &config);
    // LIST
    httpd_uri_t list_uri = {
        .uri = "/list",
        .method = HTTP_GET,
        .handler = list_handler};
    httpd_register_uri_handler(server, &list_uri);

    // UPLOAD
    httpd_uri_t upload_uri = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_handler};
    httpd_register_uri_handler(server, &upload_uri);
    // DOWNLOAD
    httpd_uri_t download_uri = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_handler};

    httpd_register_uri_handler(server, &download_uri);
    // DELETE
    httpd_uri_t delete_uri = {
        .uri = "/delete",
        .method = HTTP_POST,
        .handler = delete_handler};
    httpd_register_uri_handler(server, &delete_uri);
    httpd_uri_t print_uri = {
        .uri = "/print",
        .method = HTTP_POST,
        .handler = print_handler};

    httpd_register_uri_handler(server, &print_uri);
    httpd_uri_t uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = NULL};

    httpd_register_uri_handler(server, &uri);
}
