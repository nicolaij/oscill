#include "main.h"

#include "esp_system.h"
#include "esp_spi_flash.h"

#include "driver/temp_sensor.h"

#include "nvs.h"
#include "nvs_flash.h"

void app_main()
{

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    static const char *TAG = "TempSensor";
    //    ESP_LOGI(TAG, "Initializing Temperature sensor");
    float tsens_out;
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor_get_config(&temp_sensor);
    // ESP_LOGI(TAG, "default dac %d, clk_div %d", temp_sensor.dac_offset, temp_sensor.clk_div);
    temp_sensor.dac_offset = TSENS_DAC_DEFAULT; // DEFAULT: range:-10℃ ~  80℃, error < 1℃.
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
    temp_sensor_read_celsius(&tsens_out);
    ESP_LOGI(TAG, "Temperature out celsius %f°C", tsens_out);
    temp_sensor_stop();

    //  Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // xTaskCreate(dual_adc, "dual_adc", 1024 * 4, NULL, 6, NULL);
    xTaskCreate(adc_dma_task, "adc_dma_task", 1024 * 5, NULL, 5, NULL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    xTaskCreate(wifi_task, "wifi_task", 1024 * 5, NULL, 5, NULL);

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    };
}
