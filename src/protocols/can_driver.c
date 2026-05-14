/**
 * CAN Bus (TWAI) Protocol Driver
 * Standard and Extended frames, configurable baud rate and filters
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/twai.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "can_drv";

static bool s_initialized = false;
static can_config_params_t s_config;

static twai_timing_config_t get_timing(uint32_t baud)
{
    switch (baud) {
        case 125000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();  return t; }
        case 250000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();  return t; }
        case 1000000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();    return t; }
        default:      { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  return t; }
    }
}

static sig_err_t can_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    can_config_params_t *cfg = (can_config_params_t *)config;
    if (!cfg) {
        s_config.baud_rate = DEFAULT_CAN_BAUD;
        s_config.listen_only = false;
        s_config.filter_id = 0;
        s_config.filter_mask = 0;
        s_config.extended_frame = false;
    } else {
        memcpy(&s_config, cfg, sizeof(can_config_params_t));
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX,
        s_config.listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = get_timing(s_config.baud_rate);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (s_config.filter_mask != 0) {
        f_config.acceptance_code = s_config.filter_id;
        f_config.acceptance_mask = s_config.filter_mask;
        f_config.single_filter = true;
    }

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return SIG_ERR_HARDWARE;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "CAN initialized @ %lu baud, %s",
             s_config.baud_rate, s_config.listen_only ? "listen-only" : "normal");
    return SIG_OK;
}

static sig_err_t can_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    twai_stop();
    twai_driver_uninstall();
    s_initialized = false;
    return SIG_OK;
}

static sig_err_t can_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (len < 5) return SIG_ERR_INVALID_PARAM;  /* Minimum: 4-byte ID + 1 byte data */

    twai_message_t msg = {0};
    msg.identifier = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    msg.extd = s_config.extended_frame ? 1 : 0;
    msg.data_length_code = (len - 4 > 8) ? 8 : (len - 4);
    memcpy(msg.data, &data[4], msg.data_length_code);

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    return (err == ESP_OK) ? SIG_OK : SIG_ERR_HARDWARE;
}

static sig_err_t can_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    twai_message_t msg;
    esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) return SIG_ERR_TIMEOUT;

    /* Pack: 4 bytes ID + data */
    buf[0] = (msg.identifier >> 24) & 0xFF;
    buf[1] = (msg.identifier >> 16) & 0xFF;
    buf[2] = (msg.identifier >> 8) & 0xFF;
    buf[3] = msg.identifier & 0xFF;
    memcpy(&buf[4], msg.data, msg.data_length_code);
    *len = 4 + msg.data_length_code;
    return SIG_OK;
}

static sig_err_t can_drv_configure(void *config)
{
    can_drv_deinit();
    return can_drv_init(config);
}

static bool can_drv_is_ready(void) { return s_initialized; }

static sig_err_t can_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(can_config_params_t));
    return SIG_OK;
}

protocol_driver_t can_driver = {
    .type       = PROTO_CAN,
    .name       = "CAN",
    .init       = can_drv_init,
    .deinit     = can_drv_deinit,
    .send       = can_drv_send,
    .receive    = can_drv_receive,
    .configure  = can_drv_configure,
    .is_ready   = can_drv_is_ready,
    .get_status = can_drv_get_status,
};
