/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdint.h>
#include <string.h>
#include <lwip/netdb.h>
#include <sys/param.h>
#include <sys/types.h>

#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "portmacro.h"

#include "core1.h"
#include "tipos.h"
#include "globales.h"
#include "secrets.h"



#define EXAMPLE_ESP_MAXIMUM_RETRY  50

#define PORT                        3333
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3


#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK


struct PIXEL pixeles[NUM_PIXELES];


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "WIFI TCP SERVER";

static int s_retry_num = 0;


struct PIXEL_TCP buffer[NUM_PIXELES] = {0};
uint8_t* buffer8_t = (uint8_t*)buffer;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void send_status(const int sock)
{
    for(uint16_t i = 0; i < NUM_PIXELES; i++)
    {
        buffer[i].index = i;
        buffer[i].hue = pixeles[i].color.hue;
        buffer[i].saturation = pixeles[i].color.saturation;
        buffer[i].value = pixeles[i].color.value;
        buffer[i].modo = pixeles[i].modo;
        buffer[i].offset = pixeles[i].offset;
        buffer[i].extra = pixeles[i].extra;
        memcpy(buffer[i].params_raw, pixeles[i].params.raw, PIXEL_PARAMS_SIZE);
    }

    ssize_t to_write = sizeof(buffer);
    while(to_write > 0)
    {
        ssize_t written = send(sock, buffer8_t + (sizeof(buffer) - to_write), to_write, 0);
        if(written <= 0)
        {
            ESP_LOGE(TAG, "Error occurred during sending: %d", errno);
            return;
        }
        to_write -= written;
    }
    return;
}

void receive_n(const int sock, QueueHandle_t* pixel_queue, uint16_t cantidad_total)
{
    struct PIXEL_QUEUE pixel_nuevo;
    size_t expected_total = cantidad_total * PIXEL_TCP_SIZE;
    ssize_t received_total = 0;
    ssize_t received = 0;

    while(received_total < expected_total)
    {
        received = recv(sock, buffer8_t + received_total, expected_total - received_total, 0);
        
        if(received <= 0)
        {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            return;
        }
        
        received_total += received;
    }

    for(uint16_t i = 0; i < cantidad_total; i++)
    {
        pixel_nuevo.num = buffer[i].index;
        pixel_nuevo.pixel.color.hue = buffer[i].hue;
        pixel_nuevo.pixel.color.saturation = buffer[i].saturation;
        pixel_nuevo.pixel.color.value = buffer[i].value;
        pixel_nuevo.pixel.modo = buffer[i].modo;
        pixel_nuevo.pixel.offset = buffer[i].offset;
        pixel_nuevo.pixel.extra = buffer[i].extra;
        memcpy(pixel_nuevo.pixel.params.raw, buffer[i].params_raw, PIXEL_PARAMS_SIZE);
        xQueueSendToBack(*pixel_queue, (void*) &pixel_nuevo, 0);
    }
    return;
}

void receive_stream(const int sock)
{
    ssize_t received = 0;
    ssize_t remaining;
    struct PIXEL_TCP_STREAM pixel_nuevo;
    uint8_t* stream8_t = (uint8_t*)&pixel_nuevo;

    while(true)
    {
        remaining = PIXEL_TCP_STREAM_SIZE;
        while(remaining != 0)
        {
            received = recv(sock, stream8_t + (PIXEL_TCP_STREAM_SIZE - remaining), remaining, 0);        
            
            if(received < 0)
            {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                return;
            }
            else if(received == 0)
                return;

            remaining -= received;
        }

        if(pixeles[pixel_nuevo.index].modo != ESTATICO)
            pixeles[pixel_nuevo.index].modo = ESTATICO;
    
        pixeles[pixel_nuevo.index].color.hue = pixel_nuevo.hue;
        pixeles[pixel_nuevo.index].color.saturation = pixel_nuevo.saturation;
        pixeles[pixel_nuevo.index].color.value = pixel_nuevo.value;
    }
    return;
}

void receive(const int sock, QueueHandle_t *pixel_queue)
{
    int len;
    uint16_t received = 0;
    uint16_t cantidad_total;
    do
    {
        len = recv(sock, buffer8_t + received, 2 - received, 0);
        if(len <= 0)
        {
            ESP_LOGE(TAG, "Error ocurred during receiving cantidad_total: errno %d", errno);
            return;
        }
        received += len;
    } while(received != 2);

    cantidad_total = buffer8_t[0] | (buffer8_t[1] << 8);
    
    ESP_LOGI(TAG, "Cantidad total: %d %d", cantidad_total, pdTICKS_TO_MS(xTaskGetTickCount()));
    
    if(cantidad_total == 0)
        receive_stream(sock);
    else if(cantidad_total == 1)
        send_status(sock);
    else
        receive_n(sock, pixel_queue, cantidad_total);

    ESP_LOGI(TAG, "TERMINADO %d", pdTICKS_TO_MS(xTaskGetTickCount()));
}

void tcp_server_task(void* pixel_queue)
{
    char addr_str[128];
    uint8_t addr_family =  AF_INET;
    uint8_t ip_protocol = 0;
    uint8_t keepAlive = 1;
    uint8_t keepIdle = KEEPALIVE_IDLE;
    uint8_t keepInterval = KEEPALIVE_INTERVAL;
    uint8_t keepCount = KEEPALIVE_COUNT;
    uint8_t disableNagle = 1;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    uint8_t opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    uint8_t err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }
    gpio_set_level(2, 1);
    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &disableNagle, sizeof(disableNagle));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        receive(sock, pixel_queue);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void core0_main(void* arg)
{

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    //GPIO 2 is the board LED, which is turned on when WiFi connects
    gpio_reset_pin(2);
    gpio_set_direction(2, GPIO_MODE_OUTPUT);

    tcp_server_task(arg);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000000));
    }
}



void app_main(void)
{
    QueueHandle_t pixel_queue = xQueueCreate(400, sizeof(struct PIXEL_QUEUE));
    assert(pixel_queue != NULL);

    xTaskCreatePinnedToCore(core0_main, "core0", 8192, &pixel_queue, 1, NULL, 0);
    xTaskCreatePinnedToCore(core1_main, "core1", 8192, &pixel_queue, 5, NULL, 1);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000000));
    }
}
