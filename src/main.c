/**
 * ESP32 Multi-Protocol Signal Injector & Testing Device
 * Main Application Entry Point
 *
 * Initializes all subsystems and starts the web-based control interface.
 * Connect to WiFi AP "SignalInjector" (password: signal1234) and
 * open http://192.168.4.1 in a browser to control the device.
 */
#include "core/config.h"
#include "core/task_manager.h"
#include "protocols/protocol_driver.h"
#include "injection/signal_injector.h"
#include "analysis/signal_analyzer.h"
#include "fault/fault_injector.h"
#include "testing/loopback_tester.h"
#include "ui/web_server.h"

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void status_led_task(void *pvParameter)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_LED_STATUS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    while (1) {
        gpio_set_level(PIN_LED_STATUS, 1);
        vTaskDelay(pdMS_TO_TICKS(
            task_manager_get_mode() == MODE_IDLE ? 1000 : 200));
        gpio_set_level(PIN_LED_STATUS, 0);
        vTaskDelay(pdMS_TO_TICKS(
            task_manager_get_mode() == MODE_IDLE ? 1000 : 200));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " %s v%s", DEVICE_NAME, DEVICE_VERSION);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    /* Initialize core task manager */
    ESP_LOGI(TAG, "Initializing task manager...");
    task_manager_init();

    /* Initialize protocol registry */
    ESP_LOGI(TAG, "Initializing protocol drivers...");
    protocol_registry_init();

    /* Initialize functional modules */
    ESP_LOGI(TAG, "Initializing signal injector...");
    injector_init();

    ESP_LOGI(TAG, "Initializing signal analyzer...");
    analyzer_init();

    ESP_LOGI(TAG, "Initializing fault injector...");
    fault_injector_init();

    ESP_LOGI(TAG, "Initializing test framework...");
    tester_init();

    /* Start WiFi Access Point */
    ESP_LOGI(TAG, "Starting WiFi AP...");
    wifi_ap_init(WIFI_AP_SSID, WIFI_AP_PASS);

    /* Start Web Server */
    ESP_LOGI(TAG, "Starting web server...");
    web_server_start();

    /* Start status LED blinker */
    xTaskCreate(status_led_task, "led", 2048, NULL, 1, NULL);

    char ip[16];
    wifi_ap_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Device ready!");
    ESP_LOGI(TAG, " WiFi: %s / %s", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, " Web UI: http://%s", ip);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Supported Protocols:");
    ESP_LOGI(TAG, "   UART, SPI, I2C, RS-232, RS-485");
    ESP_LOGI(TAG, "   CAN, Modbus RTU, PWM, GPIO");
    ESP_LOGI(TAG, " Functions:");
    ESP_LOGI(TAG, "   Signal Injection & Generation");
    ESP_LOGI(TAG, "   Signal Analysis & Decoding");
    ESP_LOGI(TAG, "   Fault Injection & Testing");
    ESP_LOGI(TAG, "   Loopback & BER Testing");
    ESP_LOGI(TAG, "   Waveform Generator (DAC)");
    ESP_LOGI(TAG, "========================================");
}
