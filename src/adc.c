#include "main.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "hal/adc_ll.h"

static const char *TAG = "adc";
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MEDIAN(a) (MAX(a[0], a[1]) == MAX(a[1], a[2])) ? MAX(a[0], a[2]) : MAX(a[1], MIN(a[0], a[2]))

adc_continuous_handle_t adchandle = NULL;

#define BUFFER (200 * SOC_ADC_DIGI_RESULT_BYTES)

#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
#define ACDTYPE type2
#else
#define ACDTYPE type1
#endif

static TaskHandle_t s_task_handle;

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    // Notify that ADC continuous driver has done enough number of conversions
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}

static void continuous_adc_init()
{

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUFFER * 2,
        .conv_frame_size = BUFFER,
        .flags = 0};
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adchandle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20000 * 3 , //SOC_ADC_SAMPLE_FREQ_THRES_LOW * 2,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
#else
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
#endif
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = 1;

    adc_pattern[0].atten = ADC_ATTEN_DB_12;
    adc_pattern[0].channel = ADC_CHANNEL_0;
    adc_pattern[0].unit = ADC_UNIT_1;
    adc_pattern[0].bit_width = ADC_BITWIDTH_12;

    adc_pattern[1].atten = ADC_ATTEN_DB_12;
    adc_pattern[1].channel = ADC_CHANNEL_1;
    adc_pattern[1].unit = ADC_UNIT_1;
    adc_pattern[1].bit_width = ADC_BITWIDTH_12;

    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adchandle, &dig_cfg));
};

void adc_dma_task(void *arg)
{

    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[BUFFER] = {0};
    int median_filter_current[3];
    uint8_t median_filter_current_fill = 0;
    int median_filter_setup[3];
    uint8_t median_filter_setup_fill = 0;
    int digital_filter;

    s_task_handle = xTaskGetCurrentTaskHandle();

    continuous_adc_init();

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adchandle, &cbs, NULL));

    ESP_ERROR_CHECK(adc_continuous_start(adchandle));
    ESP_LOGI(TAG, "Start");

    int err_count = 0;
    int64_t time1 = esp_timer_get_time();
    int64_t time2 = esp_timer_get_time();
    int64_t time100 = esp_timer_get_time();

    int counter = 0;

    adc_ll_digi_set_convert_limit_num(2);

    while (1)
    {
        //ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ret = adc_continuous_read(adchandle, result, BUFFER, &ret_num, ADC_MAX_DELAY);

        // ESP_LOGW(TAG, "time: %8lld; ret: %d, %d", time2 - time1, ret_num, ret);

        adc_digi_output_data_t *p = (void *)result;
        int sum_current = 0;
        int count_current = 0;
        int sum_setup = 0;
        int count_setup = 0;

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "time: %8lld; ret: %d, %x", time2 - time1, ret_num, ret);
            vTaskDelay(1);
            continue;
        }

        while ((uint8_t *)p < result + ret_num)
        {
            switch (p->ACDTYPE.channel)
            {
            case ADC_CHANNEL_0:
                median_filter_current[median_filter_current_fill % 3] = p->ACDTYPE.data;
                median_filter_current_fill++;

                if (median_filter_current_fill >= 3)
                {
                    digital_filter = MEDIAN(median_filter_current);
                    if (median_filter_current_fill >= 6)
                        median_filter_current_fill = 3;
                }
                else
                {
                    digital_filter = p->ACDTYPE.data;
                }

                sum_current += digital_filter;
                count_current++;
                break;

            case ADC_CHANNEL_1:
                median_filter_setup[median_filter_setup_fill % 3] = p->ACDTYPE.data;
                median_filter_setup_fill++;

                if (median_filter_setup_fill >= 3)
                {
                    digital_filter = MEDIAN(median_filter_setup);
                    if (median_filter_setup_fill >= 6)
                        median_filter_setup_fill = 3;
                }
                else
                {
                    digital_filter = p->ACDTYPE.data;
                }

                sum_setup += digital_filter;
                count_setup++;
                break;

            default:
                err_count++;
                break;
            }
            p++;
        }

        time2 = esp_timer_get_time();
        ESP_LOGW(TAG, "time: %8lld; cnt: %d; ret: %d, %x; err: %d", time2 - time1, count_current, ret_num, ret, err_count);
        time1 = time2;

        counter++;

        if (esp_timer_get_time() - time100 >= 100000)
        {
            ESP_LOGE(TAG, "time: %8lld; cnt: %d; ret: %d, %x; err: %d", time2 - time100, counter, ret_num, ret, err_count);
            time100 = time2;
        }
    }
}
