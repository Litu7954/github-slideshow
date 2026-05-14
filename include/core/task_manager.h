/**
 * FreeRTOS Task Manager
 * Manages all device tasks and inter-task communication
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "config.h"

/* ── Event Bits ──────────────────────────────────────────────── */
#define EVT_PROTOCOL_READY    (1 << 0)
#define EVT_INJECTION_START   (1 << 1)
#define EVT_INJECTION_STOP    (1 << 2)
#define EVT_ANALYSIS_START    (1 << 3)
#define EVT_ANALYSIS_STOP     (1 << 4)
#define EVT_FAULT_START       (1 << 5)
#define EVT_FAULT_STOP        (1 << 6)
#define EVT_TEST_START        (1 << 7)
#define EVT_TEST_STOP         (1 << 8)
#define EVT_WIFI_CONNECTED    (1 << 9)
#define EVT_DATA_READY        (1 << 10)
#define EVT_ERROR             (1 << 11)

/* ── Command Message ─────────────────────────────────────────── */
typedef enum {
    CMD_SET_PROTOCOL,
    CMD_SET_MODE,
    CMD_START,
    CMD_STOP,
    CMD_CONFIGURE,
    CMD_GET_STATUS,
    CMD_INJECT_SIGNAL,
    CMD_START_CAPTURE,
    CMD_STOP_CAPTURE,
    CMD_START_FAULT,
    CMD_STOP_FAULT,
    CMD_RUN_TEST,
    CMD_RESET,
} command_type_t;

typedef struct {
    command_type_t type;
    protocol_type_t protocol;
    device_mode_t mode;
    void *params;
    uint16_t params_len;
} command_msg_t;

/* ── Task Manager API ────────────────────────────────────────── */
sig_err_t task_manager_init(void);
sig_err_t task_manager_send_command(const command_msg_t *cmd);
EventGroupHandle_t task_manager_get_events(void);
QueueHandle_t task_manager_get_cmd_queue(void);
QueueHandle_t task_manager_get_data_queue(void);
SemaphoreHandle_t task_manager_get_mutex(void);

void task_manager_set_mode(device_mode_t mode);
device_mode_t task_manager_get_mode(void);
protocol_type_t task_manager_get_active_protocol(void);
void task_manager_set_active_protocol(protocol_type_t proto);
