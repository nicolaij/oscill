#include "main.h"

#include "freertos/queue.h"

#include "esp_system.h"

#include "driver/temperature_sensor.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_flash.h"

QueueHandle_t adc_queue;
QueueHandle_t ui_queue;

#if SOC_TEMPERATURE_SENSOR_SUPPORT_FAST_RC

float get_temperature_sensor()
{
    float internal_temp = 0;
    ESP_LOGD("main", "Initializing Temperature sensor");

    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &internal_temp));

    ESP_ERROR_CHECK(temperature_sensor_disable(temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_uninstall(temp_sensor));

    ESP_LOGI("temperature_sensor", "Internal temperature:  %.01f°C", internal_temp);
    return internal_temp;
};
#endif

void app_main()
{

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        printf("Get flash size failed");
        return;
    }

    printf("%dMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

#if SOC_TEMPERATURE_SENSOR_SUPPORT_FAST_RC
    ESP_LOGI("main", "Temperature out celsius %f°C", get_temperature_sensor());
#endif

    // IO14 the power control of the LED is IO pin
    // #define POWER_PIN 14

    // gpio_pad_select_gpio(POWER_PIN);
    /* Set the GPIO as a push/pull output */
    // gpio_set_direction(POWER_PIN, GPIO_MODE_OUTPUT);

    // ESP_LOGI(TAG, "Turning on the peripherals power");
    // gpio_set_level(POWER_PIN, 1);

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

    // adc_queue = xQueueCreate(10, BLOCK);
    ui_queue = xQueueCreate(100, 1);

    xTaskCreate(adc_dma_task, "adc_dma_task", 1024 * 6, NULL, 5, NULL);
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    // xTaskCreate(task_SSD1306i2c, "SSD1306", 1024 * 6, NULL, 5, NULL);
    // xTaskCreate(wifi_task, "wifi_task", 1024 * 6, NULL, 5, NULL);

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    };
}
