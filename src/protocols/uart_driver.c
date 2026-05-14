/**
 * UART / RS-232 / RS-485 Protocol Driver
 * Supports standard UART, RS-232 (via MAX3232), and RS-485 (via MAX485)
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "uart_drv";

static bool s_initialized = false;
static uart_config_params_t s_config;
static uint8_t s_rx_buf[PROTOCOL_BUF_SIZE];
static bool s_rs485_mode = false;

static uart_word_length_t get_word_length(uint8_t bits)
{
    switch (bits) {
        case 5: return UART_DATA_5_BITS;
        case 6: return UART_DATA_6_BITS;
        case 7: return UART_DATA_7_BITS;
        default: return UART_DATA_8_BITS;
    }
}

static uart_parity_t get_parity(uint8_t p)
{
    switch (p) {
        case 1: return UART_PARITY_ODD;
        case 2: return UART_PARITY_EVEN;
        default: return UART_PARITY_DISABLE;
    }
}

static uart_stop_bits_t get_stop_bits(uint8_t s)
{
    return (s == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

static sig_err_t uart_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    uart_config_params_t *cfg = (uart_config_params_t *)config;
    if (!cfg) {
        /* Use defaults */
        s_config.baud_rate = DEFAULT_UART_BAUD;
        s_config.data_bits = 8;
        s_config.stop_bits = 1;
        s_config.parity = 0;
        s_config.flow_control = false;
        s_config.uart_num = 1;
    } else {
        memcpy(&s_config, cfg, sizeof(uart_config_params_t));
    }

    uart_config_t uart_cfg = {
        .baud_rate = s_config.baud_rate,
        .data_bits = get_word_length(s_config.data_bits),
        .parity    = get_parity(s_config.parity),
        .stop_bits = get_stop_bits(s_config.stop_bits),
        .flow_ctrl = s_config.flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    int uart_num = s_config.uart_num;
    esp_err_t err = uart_driver_install(uart_num, PROTOCOL_BUF_SIZE * 2, PROTOCOL_BUF_SIZE * 2, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    uart_param_config(uart_num, &uart_cfg);
    uart_set_pin(uart_num, PIN_UART1_TX, PIN_UART1_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    s_initialized = true;
    ESP_LOGI(TAG, "UART%d initialized @ %lu baud", uart_num, s_config.baud_rate);
    return SIG_OK;
}

static sig_err_t uart_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    uart_driver_delete(s_config.uart_num);
    s_initialized = false;
    ESP_LOGI(TAG, "UART deinitialized");
    return SIG_OK;
}

static sig_err_t uart_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    /* RS-485: assert DE/RE before transmitting */
    if (s_rs485_mode) {
        gpio_set_level(PIN_RS485_DE_RE, 1);
    }

    int written = uart_write_bytes(s_config.uart_num, (const char *)data, len);

    if (s_rs485_mode) {
        uart_wait_tx_done(s_config.uart_num, pdMS_TO_TICKS(100));
        gpio_set_level(PIN_RS485_DE_RE, 0);
    }

    return (written == len) ? SIG_OK : SIG_ERR_HARDWARE;
}

static sig_err_t uart_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    int read = uart_read_bytes(s_config.uart_num, s_rx_buf,
                                *len < sizeof(s_rx_buf) ? *len : sizeof(s_rx_buf),
                                pdMS_TO_TICKS(timeout_ms));
    if (read < 0) return SIG_ERR_HARDWARE;
    if (read == 0) return SIG_ERR_TIMEOUT;

    memcpy(buf, s_rx_buf, read);
    *len = (uint16_t)read;
    return SIG_OK;
}

static sig_err_t uart_drv_configure(void *config)
{
    if (!config) return SIG_ERR_INVALID_PARAM;
    uart_drv_deinit();
    return uart_drv_init(config);
}

static bool uart_drv_is_ready(void)
{
    return s_initialized;
}

static sig_err_t uart_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(uart_config_params_t));
    return SIG_OK;
}

/* RS-485 specific init */
sig_err_t rs485_init(void *config)
{
    /* Configure DE/RE pin */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_RS485_DE_RE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_RS485_DE_RE, 0);  /* Receive mode by default */
    s_rs485_mode = true;

    return uart_drv_init(config);
}

/* Driver instances */
protocol_driver_t uart_driver = {
    .type       = PROTO_UART,
    .name       = "UART",
    .init       = uart_drv_init,
    .deinit     = uart_drv_deinit,
    .send       = uart_drv_send,
    .receive    = uart_drv_receive,
    .configure  = uart_drv_configure,
    .is_ready   = uart_drv_is_ready,
    .get_status = uart_drv_get_status,
};

protocol_driver_t rs232_driver = {
    .type       = PROTO_RS232,
    .name       = "RS-232",
    .init       = uart_drv_init,  /* Same as UART with MAX3232 transceiver */
    .deinit     = uart_drv_deinit,
    .send       = uart_drv_send,
    .receive    = uart_drv_receive,
    .configure  = uart_drv_configure,
    .is_ready   = uart_drv_is_ready,
    .get_status = uart_drv_get_status,
};

protocol_driver_t rs485_driver = {
    .type       = PROTO_RS485,
    .name       = "RS-485",
    .init       = rs485_init,
    .deinit     = uart_drv_deinit,
    .send       = uart_drv_send,
    .receive    = uart_drv_receive,
    .configure  = uart_drv_configure,
    .is_ready   = uart_drv_is_ready,
    .get_status = uart_drv_get_status,
};
