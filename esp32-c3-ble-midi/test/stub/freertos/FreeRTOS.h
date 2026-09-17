#pragma once
#include <stdint.h>
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
typedef void* TaskHandle_t;
extern int delay_calls;
static inline void vTaskDelay(uint32_t t){(void)t; delay_calls++;}
