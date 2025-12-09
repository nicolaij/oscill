#pragma once

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include <esp_log.h>

#define BLOCK 1000

extern QueueHandle_t adc_queue;
extern QueueHandle_t ui_queue;

void wifi_task(void *arg);
void adc_dma_task(void *arg);
void task_SSD1306i2c(void *ignore);

