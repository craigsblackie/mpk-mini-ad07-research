#pragma once
#include "FreeRTOS.h"
static inline int xTaskCreate(void(*f)(void*),const char*n,int s,void*p,int pr,TaskHandle_t*h){(void)f;(void)n;(void)s;(void)p;(void)pr; if(h)*h=(void*)1; return 1;}
static inline uint32_t ulTaskNotifyTake(int c,uint32_t t){(void)c;(void)t;return 0;}
extern int task_notify_give_count;
static inline void xTaskNotifyGive(TaskHandle_t h){(void)h;task_notify_give_count++;}
