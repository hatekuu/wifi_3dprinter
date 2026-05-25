#ifndef ROUTER_H
#define ROUTER_H

#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "esp_spiffs.h"

#include "esp_http_server.h"

#include "esp_netif.h"

#include "main.h"

// ================= WIFI =================

void wifi_init(void);

void start_webserver(void);
void init_spiffs(void);
#endif