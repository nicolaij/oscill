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

#include "main.h"

#define DEFAULT_VREF 1100 // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES 64  // Multisampling

static esp_adc_cal_characteristics_t *adc_chars;

static const adc_channel_t channel = ADC1_CHANNEL_8; // GPIO7 if ADC1, GPIO17 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;

static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

static void check_efuse(void)
{

    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK)
    {
        printf("eFuse Two Point: Supported\n");
    }
    else
    {
        printf("Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        printf("Characterized using Two Point Value\n");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        printf("Characterized using eFuse Vref\n");
    }
    else
    {
        printf("Characterized using Default Vref\n");
    }
}

void adc1_task(void *arg)
{
    // Check if Two Point or Vref are burned into eFuse
    check_efuse();

    // Configure ADC
    if (unit == ADC_UNIT_1)
    {
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);
    }
    else
    {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }

    // Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    // Continuously sample ADC1
    while (1)
    {
        uint32_t adc_reading = 0;
        int64_t t1 = esp_timer_get_time();
        for (int i = 0; i < 100000; i++)
        {
            adc_reading = adc1_get_raw((adc1_channel_t)channel);
        }
        int64_t t2 = esp_timer_get_time();

        printf("Raw: %d\ttime: %lld\n", adc_reading, t2 - t1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void isr_adc(void *z)
{
    ets_printf("ADC INTR\n");

    adc_digi_intr_clear(ADC_UNIT_1, ADC_DIGI_INTR_MASK_MONITOR);
}

void adc_dma_task(void *arg)
{

    //  controller_clk = (APLL or APB) / (div_num + div_a / div_b + 1).
    adc_digi_clk_t adc_clk = {.use_apll = false, //!< true: use APLL clock; false: use APB clock.
                              .div_num = 9,      //!< Division factor. Range: 0 ~ 255.
                                                 // Note: When a higher frequency clock is used (the division factor is less than 9),
                                                 // the ADC reading value will be slightly offset.
                              .div_b = 1,        //!< Division factor. Range: 1 ~ 63.
                              .div_a = 0};       //!< Division factor. Range: 0 ~ 63.

    adc_digi_pattern_table_t adc1_pattern = {
        .atten = ADC_ATTEN_DB_11,   /*!< ADC sampling voltage attenuation configuration. */
        .channel = ADC1_CHANNEL_8}; /*!< ADC channel index. */

    adc_digi_config_t adc_config = {.conv_limit_en = false,        /*!<Enable the function of limiting ADC conversion times.
                                                                  If the number of ADC conversion trigger count is equal to the `limit_num`, the conversion is stopped. */
                                    .conv_limit_num = 0,           /*!<Set the upper limit of the number of ADC conversion triggers. Range: 1 ~ 255. */
                                    .adc1_pattern_len = 1,         /*!<Pattern table length for digital controller. Range: 0 ~ 16 (0: Don't change the pattern table setting).
                                                                   The pattern table that defines the conversion rules for each SAR ADC. Each table has 16 items, in which channel selection,
                                                                   resolution and attenuation are stored. When the conversion is started, the controller reads conversion rules from the
                                                                   pattern table one by one. For each controller the scan sequence has at most 16 different rules before repeating itself. */
                                    .adc2_pattern_len = 0,         /*!<Refer to ``adc1_pattern_len`` */
                                    .adc1_pattern = &adc1_pattern, /*!<Pointer to pattern table for digital controller. The table size defined by `adc1_pattern_len`. */
                                    .adc2_pattern = 0,             /*!<Refer to `adc1_pattern` */
                                    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                    .format = ADC_DIGI_FORMAT_12BIT,
                                    .interval = 40,     //!< The number of interval clock cycles for the digital controller to trigger the measurement.
                                                        // The unit is the divided clock. Range: 40 ~ 4095.
                                                        // Expression: `trigger_meas_freq` = `controller_clk` / 2 / interval. Refer to ``adc_digi_clk_t``.
                                                        // Note: The sampling rate of each channel is also related to the conversion mode (See ``adc_digi_convert_mode_t``) and pattern table settings. */
                                    .dig_clk = adc_clk, //!< ADC digital controller clock divider settings. Refer to ``adc_digi_clk_t``.
                                                        // Note: The clocks of the DAC digital controller use the ADC digital controller clock divider.
                                    .dma_eof_num = 64}; //!< DMA eof num of adc digital controller.
                                                        // If the number of measurements reaches `dma_eof_num`, then `dma_in_suc_eof` signal is generated in DMA.
                                                        // Note: The converted d}

    //Configura o modo monitor (Threshold) ao ADC via DMA
    adc_digi_monitor_t mcfg;
    mcfg.adc_unit = ADC_UNIT_1;
    mcfg.channel = ADC_CHANNEL_MAX;
    mcfg.mode = ADC_DIGI_MONITOR_LOW; //Gera interrupcao quando HIGH ou LOW
    mcfg.threshold = 2048; //Valor limite para gerar interrupcao

    //Habilita o modo digital ao ADC
    ESP_ERROR_CHECK(adc_digi_init());
    ESP_ERROR_CHECK(adc_digi_controller_config(&adc_config));
    
    //Habilita o modo monitor ao ADC
    ESP_ERROR_CHECK(adc_digi_monitor_set_config(ADC_DIGI_MONITOR_IDX0, &mcfg));
    ESP_ERROR_CHECK(adc_digi_monitor_enable(ADC_DIGI_MONITOR_IDX0, true));

    //Configura ISR ao modo monitor
    ESP_ERROR_CHECK(adc_digi_isr_register(isr_adc, NULL, 0));
    ESP_ERROR_CHECK(adc_digi_intr_enable(ADC_UNIT_1, ADC_DIGI_INTR_MASK_MONITOR));
    ESP_ERROR_CHECK(adc_digi_start());

    while (1)
    {
        ESP_LOGI("main", "teste");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
