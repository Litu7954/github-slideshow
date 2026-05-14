/**
 * Modbus RTU/TCP Protocol Driver
 * Master/Slave Modbus implementation over RS-485 and TCP
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "modbus_drv";

static bool s_initialized = false;
static modbus_config_params_t s_config;
static uint8_t s_rx_buf[PROTOCOL_BUF_SIZE];

/* Modbus CRC-16 calculation */
static uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static sig_err_t modbus_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    modbus_config_params_t *cfg = (modbus_config_params_t *)config;
    if (!cfg) {
        s_config.baud_rate = 9600;
        s_config.slave_id = 1;
        s_config.is_master = true;
        s_config.parity = 2;  /* Even parity (Modbus default) */
        s_config.is_tcp = false;
        s_config.tcp_port = 502;
    } else {
        memcpy(&s_config, cfg, sizeof(modbus_config_params_t));
    }

    if (!s_config.is_tcp) {
        /* Initialize UART for Modbus RTU */
        uart_config_t uart_cfg = {
            .baud_rate = s_config.baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity    = (s_config.parity == 1) ? UART_PARITY_ODD :
                         (s_config.parity == 2) ? UART_PARITY_EVEN : UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        uart_driver_install(UART_NUM_1, PROTOCOL_BUF_SIZE * 2, PROTOCOL_BUF_SIZE * 2, 0, NULL, 0);
        uart_param_config(UART_NUM_1, &uart_cfg);
        uart_set_pin(UART_NUM_1, PIN_RS485_TX, PIN_RS485_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        /* RS-485 DE/RE pin */
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << PIN_RS485_DE_RE),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&io_conf);
        gpio_set_level(PIN_RS485_DE_RE, 0);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Modbus %s initialized, %s mode, ID=%d",
             s_config.is_tcp ? "TCP" : "RTU",
             s_config.is_master ? "master" : "slave",
             s_config.slave_id);
    return SIG_OK;
}

static sig_err_t modbus_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (!s_config.is_tcp) {
        uart_driver_delete(UART_NUM_1);
    }
    s_initialized = false;
    return SIG_OK;
}

static sig_err_t modbus_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    /* Build Modbus RTU frame with CRC */
    uint8_t frame[256];
    if (len > sizeof(frame) - 2) return SIG_ERR_BUFFER_FULL;

    memcpy(frame, data, len);
    uint16_t crc = modbus_crc16(frame, len);
    frame[len] = crc & 0xFF;
    frame[len + 1] = (crc >> 8) & 0xFF;

    /* Transmit with RS-485 direction control */
    gpio_set_level(PIN_RS485_DE_RE, 1);
    int written = uart_write_bytes(UART_NUM_1, (const char *)frame, len + 2);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RS485_DE_RE, 0);

    return (written == len + 2) ? SIG_OK : SIG_ERR_HARDWARE;
}

static sig_err_t modbus_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    /* Wait for response with inter-frame gap detection */
    int read = uart_read_bytes(UART_NUM_1, s_rx_buf, sizeof(s_rx_buf), pdMS_TO_TICKS(timeout_ms));
    if (read <= 0) return SIG_ERR_TIMEOUT;
    if (read < 4) return SIG_ERR_PROTOCOL;  /* Minimum frame: addr + func + 2 CRC */

    /* Verify CRC */
    uint16_t received_crc = s_rx_buf[read - 2] | (s_rx_buf[read - 1] << 8);
    uint16_t calc_crc = modbus_crc16(s_rx_buf, read - 2);
    if (received_crc != calc_crc) {
        ESP_LOGW(TAG, "CRC mismatch: 0x%04X vs 0x%04X", received_crc, calc_crc);
        return SIG_ERR_PROTOCOL;
    }

    uint16_t copy_len = (read - 2 < *len) ? read - 2 : *len;
    memcpy(buf, s_rx_buf, copy_len);
    *len = copy_len;
    return SIG_OK;
}

static sig_err_t modbus_drv_configure(void *config)
{
    modbus_drv_deinit();
    return modbus_drv_init(config);
}

static bool modbus_drv_is_ready(void) { return s_initialized; }

static sig_err_t modbus_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(modbus_config_params_t));
    return SIG_OK;
}

/* ── Modbus Master Functions ─────────────────────────────────── */

sig_err_t modbus_read_holding_registers(uint8_t slave_id, uint16_t reg, uint16_t count,
                                         uint16_t *values)
{
    uint8_t request[6] = {
        slave_id,
        0x03,  /* Function code: Read Holding Registers */
        (reg >> 8) & 0xFF, reg & 0xFF,
        (count >> 8) & 0xFF, count & 0xFF,
    };

    sig_err_t err = modbus_drv_send(request, sizeof(request));
    if (err != SIG_OK) return err;

    uint8_t response[256];
    uint16_t resp_len = sizeof(response);
    err = modbus_drv_receive(response, &resp_len, 1000);
    if (err != SIG_OK) return err;

    if (response[1] & 0x80) return SIG_ERR_PROTOCOL;  /* Exception response */

    uint8_t byte_count = response[2];
    for (int i = 0; i < byte_count / 2 && i < count; i++) {
        values[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    return SIG_OK;
}

sig_err_t modbus_write_single_register(uint8_t slave_id, uint16_t reg, uint16_t value)
{
    uint8_t request[6] = {
        slave_id,
        0x06,  /* Function code: Write Single Register */
        (reg >> 8) & 0xFF, reg & 0xFF,
        (value >> 8) & 0xFF, value & 0xFF,
    };
    return modbus_drv_send(request, sizeof(request));
}

protocol_driver_t modbus_rtu_driver = {
    .type       = PROTO_MODBUS_RTU,
    .name       = "Modbus RTU",
    .init       = modbus_drv_init,
    .deinit     = modbus_drv_deinit,
    .send       = modbus_drv_send,
    .receive    = modbus_drv_receive,
    .configure  = modbus_drv_configure,
    .is_ready   = modbus_drv_is_ready,
    .get_status = modbus_drv_get_status,
};
