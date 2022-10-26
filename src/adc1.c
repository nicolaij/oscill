/* ADC1 Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "soc/lldesc.h"

#include "main.h"

#include "esp_log.h"

#define BLOCK 1000

uint8_t result[BLOCK] = {0};

static const char *TAG = "ADC DMA";

static bool check_valid_data(const adc_digi_output_data_t *data)
{
    const unsigned int unit = data->type2.unit;
    if (unit > 2)
        return false;
    if (data->type2.channel >= SOC_ADC_CHANNEL_NUM(unit))
        return false;

    return true;
}

void adc_dma_task(void *arg)
{

    adc_digi_init_config_t adc_dma_config = {
        .max_store_buf_size = BLOCK * 4,
        .conv_num_each_intr = BLOCK,
        .adc1_chan_mask = BIT(ADC1_CHANNEL_7),
        .adc2_chan_mask = 0,
    };
    ESP_ERROR_CHECK(adc_digi_initialize(&adc_dma_config));

    adc_digi_configuration_t dig_cfg = {
        .conv_limit_en = false,
        .conv_limit_num = 100,
        .sample_freq_hz = 10 * 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = 1;

    adc_pattern[0].atten = ADC_ATTEN_DB_11;
    adc_pattern[0].channel = ADC1_CHANNEL_7;
    adc_pattern[0].unit = 0;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_digi_controller_configure(&dig_cfg));

    ESP_ERROR_CHECK(adc_digi_start());
    esp_err_t ret;
    uint32_t ret_num = 0;

    while (1)
    {
        int req = BLOCK;
        uint8_t *p = result;
        while (req > 0)
        {
            ret = adc_digi_read_bytes(p, req, &ret_num, ADC_MAX_DELAY);

            if (ret == ESP_OK)
            {
                // ESP_LOGI(TAG, "ret is %x, ret_num is %d", ret, ret_num);
                //  check data
                for (int i = 0; i < ret_num; i += 2)
                {
                    adc_digi_output_data_t *adcp = (void *)&p[i];
                    if (adcp->type1.channel != ADC1_CHANNEL_7)
                        ESP_LOGE(TAG, "channel error: %d", adcp->type1.channel);
                }
            }

            req = req - ret_num;
            p = p + ret_num;
        }

        /*
                    for (int i = 0; i < ret_num; i += 2)
                    {
                        adc_digi_output_data_t *p = (void *)&result[i];

                        if (check_valid_data(p))
                        {
                            ESP_LOGI(TAG, "Unit: %d,_Channel: %d, Value: %x", p->type2.unit + 1, p->type2.channel, p->type2.data);
                        }
                        else
                        {
                            // abort();
                            ESP_LOGI(TAG, "Invalid data [%d_%d_%x]", p->type2.unit + 1, p->type2.channel, p->type2.data);
                        }
                    }
        */
        // See `note 1`
        vTaskDelay(1);
    }

    while (1)
    {
        ESP_LOGI("main", "exit");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
