/**
 * SPI Protocol Driver
 * Full-duplex SPI master with configurable mode, clock, and CS
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "spi_drv";

static bool s_initialized = false;
static spi_config_params_t s_config;
static spi_device_handle_t s_spi_handle;
static uint8_t s_rx_buf[PROTOCOL_BUF_SIZE];
static uint16_t s_rx_len = 0;

static sig_err_t spi_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    spi_config_params_t *cfg = (spi_config_params_t *)config;
    if (!cfg) {
        s_config.clock_hz = DEFAULT_SPI_FREQ;
        s_config.mode = 0;
        s_config.bit_order = 0;
        s_config.cs_pin = PIN_SPI_CS0;
        s_config.full_duplex = true;
    } else {
        memcpy(&s_config, cfg, sizeof(spi_config_params_t));
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PROTOCOL_BUF_SIZE,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = s_config.clock_hz,
        .mode = s_config.mode,
        .spics_io_num = s_config.cs_pin,
        .queue_size = 4,
        .flags = s_config.bit_order ? SPI_DEVICE_BIT_LSBFIRST : 0,
    };

    if (!s_config.full_duplex) {
        dev_cfg.flags |= SPI_DEVICE_HALFDUPLEX;
    }

    err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        spi_bus_free(SPI2_HOST);
        return SIG_ERR_HARDWARE;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SPI initialized @ %lu Hz, mode %d", s_config.clock_hz, s_config.mode);
    return SIG_OK;
}

static sig_err_t spi_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    spi_bus_remove_device(s_spi_handle);
    spi_bus_free(SPI2_HOST);
    s_initialized = false;
    ESP_LOGI(TAG, "SPI deinitialized");
    return SIG_OK;
}

static sig_err_t spi_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .rx_buffer = s_rx_buf,
    };

    esp_err_t err = spi_device_transmit(s_spi_handle, &trans);
    if (err != ESP_OK) return SIG_ERR_HARDWARE;

    s_rx_len = len;
    return SIG_OK;
}

static sig_err_t spi_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (s_rx_len == 0) return SIG_ERR_TIMEOUT;

    uint16_t copy_len = (*len < s_rx_len) ? *len : s_rx_len;
    memcpy(buf, s_rx_buf, copy_len);
    *len = copy_len;
    s_rx_len = 0;
    return SIG_OK;
}

static sig_err_t spi_drv_configure(void *config)
{
    spi_drv_deinit();
    return spi_drv_init(config);
}

static bool spi_drv_is_ready(void) { return s_initialized; }

static sig_err_t spi_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(spi_config_params_t));
    return SIG_OK;
}

protocol_driver_t spi_driver = {
    .type       = PROTO_SPI,
    .name       = "SPI",
    .init       = spi_drv_init,
    .deinit     = spi_drv_deinit,
    .send       = spi_drv_send,
    .receive    = spi_drv_receive,
    .configure  = spi_drv_configure,
    .is_ready   = spi_drv_is_ready,
    .get_status = spi_drv_get_status,
};
