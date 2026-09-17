#pragma once
#include "esp_err.h"
typedef struct esp_netif_obj esp_netif_t;
static inline esp_err_t esp_netif_init(void){return 0;}
static inline esp_netif_t *esp_netif_create_default_wifi_ap(void){return 0;}
