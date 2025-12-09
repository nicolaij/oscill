/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "main.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif.h"

#include <esp_http_server.h>

#include "esp_spiffs.h"

#include "driver/uart.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define CLIENT_WIFI_SSID "Nadtocheeva 5"
#define CLIENT_WIFI_PASS "123123123"
#define AP_WIFI_SSID "oscill"
#define AP_WIFI_PASS "123123123"

#define STA_ESP_MAXIMUM_RETRY 3

#define AP_ESP_WIFI_CHANNEL 1
#define AP_MAX_STA_CONN 5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* esp netif object representing the WIFI station */
static esp_netif_t *sta_netif = NULL;

#define PAGE_ROOT "/"
/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

extern int32_t id;

static const char *TAG = "wifi";

static const char *TAGH = "httpd";

static int s_retry_num = 0;

char buf[CONFIG_LWIP_TCP_MSS];

size_t buf_len;

int64_t timeout_begin;

bool need_ws_send = false;

bool restart = false;

typedef struct
{
    char filepath[32];
    char content[32];
} down_data_t;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *sta_disconnect_evt = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "wifi disconnect reason:%d", sta_disconnect_evt->reason);
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Station started");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_softap(void)
{

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_WIFI_SSID,
            .ssid_len = strlen(AP_WIFI_SSID),
            .channel = AP_ESP_WIFI_CHANNEL,
            .password = AP_WIFI_PASS,
            .max_connection = AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    if (strlen(AP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    static char wifi_name[32] = AP_WIFI_SSID;
    // int l = strlen(wifi_name);
    // itoa(id, &wifi_name[l], 10);

    strlcpy((char *)wifi_config.ap.ssid, wifi_name, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(wifi_name);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d", AP_WIFI_SSID, AP_WIFI_PASS, AP_ESP_WIFI_CHANNEL);
}

esp_err_t wifi_init_sta(void)
{
    esp_err_t ret_value = ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CLIENT_WIFI_SSID,
            .password = CLIENT_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CLIENT_WIFI_SSID, CLIENT_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CLIENT_WIFI_SSID, CLIENT_WIFI_PASS);
        ret_value = ESP_FAIL;
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        ret_value = ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
    return ret_value;
}

static esp_err_t download_get_handler(httpd_req_t *req)
{
    char *filepath = ((down_data_t *)(req->user_ctx))->filepath;
    FILE *fd = NULL;
    struct stat file_stat;

    if (stat(filepath, &file_stat) == -1)
    {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filepath, file_stat.st_size);

    httpd_resp_set_type(req, ((down_data_t *)(req->user_ctx))->content);
    int l = strlen(filepath);
    if (filepath[l - 3] == '.' && filepath[l - 2] == 'g' && filepath[l - 1] == 'z')
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    size_t chunksize;
    int n = 0;
    do
    {
        // memset(buf, 0, sizeof(buf));
        chunksize = fread(buf, 1, sizeof(buf), fd);
        // printf("fread %d\n", chunksize);

        if (chunksize > 0)
        {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, buf, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File %s sending failed!", filepath);
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    // ESP_LOGI(TAG, "File sending complete");

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
};

httpd_handle_t ws_hd;
int ws_fd = 0;

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(char *msg)
{
    if (ws_fd == 0)
        return;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)msg;
    ws_pkt.len = strlen(msg);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(ws_hd, ws_fd, &ws_pkt);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WS handshake done, the new connection was opened");
        return ESP_OK;
    }

    uint8_t bf[160] = {0};
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = bf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(bf));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAGH, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }
    ESP_LOGI(TAGH, "Got packet with message: \"%s\"", ws_pkt.payload);
    ESP_LOGI(TAGH, "Packet type: %d", ws_pkt.type);

    ws_hd = req->handle;
    ws_fd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAGH, "ws_hd/fd: %d/%d", *(int *)ws_hd, ws_fd);

    if (strcmp("open ws", (const char *)ws_pkt.payload) == 0)
        need_ws_send = true;

    return ret;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = download_get_handler,
    .user_ctx = &((down_data_t){.filepath = "/spiffs/index.html", .content = "text/html"}),
    .is_websocket = false};

static const httpd_uri_t ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true};

static const httpd_uri_t d3_get = {
    .uri = "/d3",
    .method = HTTP_GET,
    .handler = download_get_handler,
    .user_ctx = &((down_data_t){.filepath = "/spiffs/D3.html", .content = "text/html"}),
    .is_websocket = false};

static const httpd_uri_t d3_get_gz = {
    .uri = "/d3.min.js",
    .method = HTTP_GET,
    .handler = download_get_handler,
    .user_ctx = &((down_data_t){.filepath = "/spiffs/d3.min.js.gz", .content = "application/javascript"}),
    .is_websocket = false};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // config.max_open_sockets = 2;
    // config.stack_size = 1024 * 10;
    config.lru_purge_enable = true;
    // config.send_wait_timeout = 30;
    // config.recv_wait_timeout = 30;
    // config.task_priority = 6;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &d3_get);
        httpd_register_uri_handler(server, &d3_get_gz);

        ws_fd = 0;

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void wifi_task(void *arg)
{
    ESP_LOGI("SPIFFS", "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE("SPIFFS", "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE("SPIFFS", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE("SPIFFS", "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI("SPIFFS", "Partition size: total: %d, used: %d", total, used);
    }

    int wifi_on = 1;

    esp_err_t e = wifi_init_sta();
    // if (e == ESP_OK)
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // else
    //     wifi_init_softap();

    /* Start the server for the first time */
    start_webserver();

    uint32_t ret_num = 0;
    uint8_t buffer_adc_data[BLOCK];

    uint32_t sum = 0;
    uint32_t num = 0;

    while (1)
    {
        ret = 0;
        if ((ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) && ret_num > 0)
        {
            if (ws_fd > 0)
            {
                httpd_ws_frame_t ws_pkt = {
                    .final = true,
                    .fragmented = false,
                    .payload = buffer_adc_data,
                    .len = ret_num,
                    .type = HTTPD_WS_TYPE_BINARY,
                };
                httpd_ws_send_frame_async(ws_hd, ws_fd, &ws_pkt);
            }
            /*
                        adc_digi_output_data_t *p = (void *)buffer_adc_data;

                        while ((uint8_t *)p < buffer_adc_data + ret_num)
                        {
                            sum += p->type1.data;
                            num++;
                            if (num == 10)
                            {
                                uint8_t r = (sum / num) >> (12 - 5);
                                xQueueSend(ui_queue, &r, 0);
                                num = 0;
                                sum = 0;
                            }
                            p++;
                        }*/
        };

        if (restart == true)
        {
            esp_wifi_stop();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        }

        vTaskDelay(1);
    }
}
