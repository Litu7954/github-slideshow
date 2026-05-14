/**
 * FreeRTOS Task Manager Implementation
 */
#include "core/task_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "task_mgr";

static EventGroupHandle_t s_event_group;
static QueueHandle_t s_cmd_queue;
static QueueHandle_t s_data_queue;
static SemaphoreHandle_t s_mutex;
static device_mode_t s_current_mode = MODE_IDLE;
static protocol_type_t s_active_protocol = PROTO_NONE;

sig_err_t task_manager_init(void)
{
    s_event_group = xEventGroupCreate();
    if (!s_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return SIG_ERR_HARDWARE;
    }

    s_cmd_queue = xQueueCreate(16, sizeof(command_msg_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return SIG_ERR_HARDWARE;
    }

    s_data_queue = xQueueCreate(32, sizeof(signal_packet_t));
    if (!s_data_queue) {
        ESP_LOGE(TAG, "Failed to create data queue");
        return SIG_ERR_HARDWARE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return SIG_ERR_HARDWARE;
    }

    ESP_LOGI(TAG, "Task manager initialized");
    return SIG_OK;
}

sig_err_t task_manager_send_command(const command_msg_t *cmd)
{
    if (!cmd) return SIG_ERR_INVALID_PARAM;
    if (xQueueSend(s_cmd_queue, cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full");
        return SIG_ERR_BUFFER_FULL;
    }
    return SIG_OK;
}

EventGroupHandle_t task_manager_get_events(void)
{
    return s_event_group;
}

QueueHandle_t task_manager_get_cmd_queue(void)
{
    return s_cmd_queue;
}

QueueHandle_t task_manager_get_data_queue(void)
{
    return s_data_queue;
}

SemaphoreHandle_t task_manager_get_mutex(void)
{
    return s_mutex;
}

void task_manager_set_mode(device_mode_t mode)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_current_mode = mode;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Mode changed to %d", mode);
}

device_mode_t task_manager_get_mode(void)
{
    device_mode_t mode;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    mode = s_current_mode;
    xSemaphoreGive(s_mutex);
    return mode;
}

protocol_type_t task_manager_get_active_protocol(void)
{
    protocol_type_t proto;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    proto = s_active_protocol;
    xSemaphoreGive(s_mutex);
    return proto;
}

void task_manager_set_active_protocol(protocol_type_t proto)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active_protocol = proto;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Active protocol: %d", proto);
}
