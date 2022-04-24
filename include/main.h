#pragma once

#include <stdio.h> 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include <esp_log.h>

QueueHandle_t ws_send_queue;

void wifi_task(void *arg);
void adc1_task(void *arg);
void adc_dma_task(void *arg);

