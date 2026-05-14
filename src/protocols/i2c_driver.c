/**
 * I2C Protocol Driver
 * Master/Slave I2C with bus scanning and device enumeration
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "i2c_drv";

static bool s_initialized = false;
static i2c_config_params_t s_config;
static uint8_t s_rx_buf[PROTOCOL_BUF_SIZE];

static sig_err_t i2c_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    i2c_config_params_t *cfg = (i2c_config_params_t *)config;
    if (!cfg) {
        s_config.clock_hz = DEFAULT_I2C_FREQ;
        s_config.address = 0x00;
        s_config.is_master = true;
        s_config.addr_10bit = false;
    } else {
        memcpy(&s_config, cfg, sizeof(i2c_config_params_t));
    }

    i2c_config_t i2c_cfg = {
        .mode = s_config.is_master ? I2C_MODE_MASTER : I2C_MODE_SLAVE,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = s_config.clock_hz,
    };

    esp_err_t err = i2c_param_config(I2C_NUM_0, &i2c_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    err = i2c_driver_install(I2C_NUM_0,
                              s_config.is_master ? I2C_MODE_MASTER : I2C_MODE_SLAVE,
                              s_config.is_master ? 0 : PROTOCOL_BUF_SIZE,
                              s_config.is_master ? 0 : PROTOCOL_BUF_SIZE,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2C initialized @ %lu Hz, %s mode",
             s_config.clock_hz, s_config.is_master ? "master" : "slave");
    return SIG_OK;
}

static sig_err_t i2c_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    i2c_driver_delete(I2C_NUM_0);
    s_initialized = false;
    return SIG_OK;
}

static sig_err_t i2c_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (len < 1) return SIG_ERR_INVALID_PARAM;

    /* First byte is device address, remaining is data */
    uint8_t addr = data[0];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len > 1) {
        i2c_master_write(cmd, &data[1], len - 1, true);
    }
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return (err == ESP_OK) ? SIG_OK : SIG_ERR_HARDWARE;
}

static sig_err_t i2c_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (*len < 2) return SIG_ERR_INVALID_PARAM;

    uint8_t addr = buf[0];
    uint16_t read_len = *len - 1;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (read_len > 1) {
        i2c_master_read(cmd, s_rx_buf, read_len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &s_rx_buf[read_len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) return SIG_ERR_HARDWARE;

    memcpy(buf, s_rx_buf, read_len);
    *len = read_len;
    return SIG_OK;
}

static sig_err_t i2c_drv_configure(void *config)
{
    i2c_drv_deinit();
    return i2c_drv_init(config);
}

static bool i2c_drv_is_ready(void) { return s_initialized; }

static sig_err_t i2c_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(i2c_config_params_t));
    return SIG_OK;
}

/* I2C bus scanner */
sig_err_t i2c_scan_bus(uint8_t *found_addrs, uint8_t *count, uint8_t max_count)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    *count = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (err == ESP_OK && *count < max_count) {
            found_addrs[*count] = addr;
            (*count)++;
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
        }
    }
    return SIG_OK;
}

protocol_driver_t i2c_driver = {
    .type       = PROTO_I2C,
    .name       = "I2C",
    .init       = i2c_drv_init,
    .deinit     = i2c_drv_deinit,
    .send       = i2c_drv_send,
    .receive    = i2c_drv_receive,
    .configure  = i2c_drv_configure,
    .is_ready   = i2c_drv_is_ready,
    .get_status = i2c_drv_get_status,
};
