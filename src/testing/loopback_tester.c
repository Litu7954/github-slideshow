/**
 * Loopback & BER Testing Module Implementation
 */
#include "testing/loopback_tester.h"
#include "protocols/protocol_driver.h"
#include "core/task_manager.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tester";

static bool s_running = false;
static float s_progress = 0.0f;
static test_result_t s_last_result;

/* PRBS state for BER testing */
static uint32_t s_ber_prbs = 0x1;

static uint8_t ber_prbs_byte(uint8_t order)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t fb;
        switch (order) {
            case 7:  fb = ((s_ber_prbs >> 6) ^ (s_ber_prbs >> 5)) & 1; break;
            case 9:  fb = ((s_ber_prbs >> 8) ^ (s_ber_prbs >> 4)) & 1; break;
            case 15: fb = ((s_ber_prbs >> 14) ^ (s_ber_prbs >> 13)) & 1; break;
            case 23: fb = ((s_ber_prbs >> 22) ^ (s_ber_prbs >> 17)) & 1; break;
            default: fb = ((s_ber_prbs >> 10) ^ (s_ber_prbs >> 6)) & 1; break;
        }
        s_ber_prbs = (s_ber_prbs << 1) | fb;
        byte = (byte << 1) | (uint8_t)fb;
    }
    return byte;
}

static void generate_test_data(uint8_t *buf, uint16_t len, uint8_t pattern, uint16_t seq)
{
    switch (pattern) {
        case 0: /* Counter */
            for (uint16_t i = 0; i < len; i++) buf[i] = (seq + i) & 0xFF;
            break;
        case 1: /* PRBS */
            for (uint16_t i = 0; i < len; i++) buf[i] = ber_prbs_byte(15);
            break;
        case 2: /* Walking ones */
            for (uint16_t i = 0; i < len; i++) buf[i] = 1 << (i % 8);
            break;
        case 3: /* 0xAA pattern */
            memset(buf, 0xAA, len);
            break;
        case 4: /* 0x55 pattern */
            memset(buf, 0x55, len);
            break;
        default:
            memset(buf, 0xFF, len);
            break;
    }
}

/* ── Public API ──────────────────────────────────────────────── */

sig_err_t tester_init(void)
{
    s_running = false;
    s_progress = 0.0f;
    memset(&s_last_result, 0, sizeof(s_last_result));
    ESP_LOGI(TAG, "Tester initialized");
    return SIG_OK;
}

sig_err_t tester_deinit(void)
{
    s_running = false;
    return SIG_OK;
}

sig_err_t tester_run_loopback(const test_config_t *config, loopback_mode_t mode,
                               test_result_t *result)
{
    if (s_running) return SIG_ERR_BUSY;
    s_running = true;

    protocol_driver_t *drv = protocol_get_driver(config->protocol);
    if (!drv || !drv->is_ready()) {
        s_running = false;
        return SIG_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Starting loopback test: proto=%s, packets=%lu, size=%d",
             protocol_get_name(config->protocol), config->packet_count, config->packet_size);

    result->protocol = config->protocol;
    result->total_packets = 0;
    result->error_packets = 0;
    result->dropped_packets = 0;
    result->min_latency_us = UINT32_MAX;
    result->max_latency_us = 0;
    uint64_t latency_sum = 0;

    uint8_t tx_buf[256];
    uint8_t rx_buf[256];
    uint16_t pkt_size = (config->packet_size > sizeof(tx_buf)) ? sizeof(tx_buf) : config->packet_size;

    uint32_t start_time = esp_log_timestamp();

    for (uint32_t i = 0; i < config->packet_count && s_running; i++) {
        generate_test_data(tx_buf, pkt_size, config->test_pattern, (uint16_t)i);

        int64_t send_time = esp_timer_get_time();
        sig_err_t err = drv->send(tx_buf, pkt_size);
        if (err != SIG_OK) {
            result->error_packets++;
            result->total_packets++;
            continue;
        }

        uint16_t rx_len = pkt_size;
        err = drv->receive(rx_buf, &rx_len, config->timeout_ms);

        if (err == SIG_ERR_TIMEOUT) {
            result->dropped_packets++;
        } else if (err == SIG_OK) {
            if (config->measure_latency) {
                uint32_t latency = (uint32_t)(esp_timer_get_time() - send_time);
                if (latency < result->min_latency_us) result->min_latency_us = latency;
                if (latency > result->max_latency_us) result->max_latency_us = latency;
                latency_sum += latency;
            }

            if (config->verify_data && memcmp(tx_buf, rx_buf, pkt_size) != 0) {
                result->error_packets++;
            }
        } else {
            result->error_packets++;
        }

        result->total_packets++;
        s_progress = (float)i / config->packet_count * 100.0f;

        if (config->interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(config->interval_ms));
        }
    }

    result->test_duration_ms = esp_log_timestamp() - start_time;
    if (result->total_packets > 0) {
        result->avg_latency_us = (uint32_t)(latency_sum / result->total_packets);
        result->bit_error_rate = (float)result->error_packets / result->total_packets;
        result->throughput_bps = (float)(result->total_packets * pkt_size * 8) /
                                  (result->test_duration_ms / 1000.0f);
    }
    if (result->min_latency_us == UINT32_MAX) result->min_latency_us = 0;

    memcpy(&s_last_result, result, sizeof(test_result_t));
    s_running = false;
    s_progress = 100.0f;

    ESP_LOGI(TAG, "Loopback test complete: %lu/%lu OK, BER=%.6f, throughput=%.0f bps",
             result->total_packets - result->error_packets,
             result->total_packets, result->bit_error_rate, result->throughput_bps);

    return SIG_OK;
}

sig_err_t tester_stop_loopback(void)
{
    s_running = false;
    return SIG_OK;
}

sig_err_t tester_run_ber(const ber_config_t *config, ber_result_t *result)
{
    if (s_running) return SIG_ERR_BUSY;
    s_running = true;

    protocol_driver_t *drv = protocol_get_driver(config->protocol);
    if (!drv || !drv->is_ready()) {
        s_running = false;
        return SIG_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Starting BER test: proto=%s, PRBS-%d",
             protocol_get_name(config->protocol), config->prbs_order);

    memset(result, 0, sizeof(ber_result_t));
    s_ber_prbs = 0x1;

    uint8_t tx_buf[64];
    uint8_t rx_buf[64];
    uint32_t chunk_size = 64;

    int64_t start = esp_timer_get_time();
    int64_t end_time = start + (int64_t)config->duration_sec * 1000000;

    while (s_running) {
        if (config->duration_sec > 0 && esp_timer_get_time() >= end_time) break;
        if (config->total_bits > 0 && result->total_bits_sent >= config->total_bits) break;

        /* Generate PRBS data */
        uint32_t prbs_save = s_ber_prbs;
        for (uint32_t i = 0; i < chunk_size; i++) {
            tx_buf[i] = ber_prbs_byte(config->prbs_order);
        }

        drv->send(tx_buf, chunk_size);
        result->total_bits_sent += chunk_size * 8;

        uint16_t rx_len = chunk_size;
        sig_err_t err = drv->receive(rx_buf, &rx_len, 500);

        if (err == SIG_OK) {
            result->total_bits_received += rx_len * 8;
            result->synced = true;

            /* Compare bits */
            s_ber_prbs = prbs_save;
            for (uint32_t i = 0; i < rx_len; i++) {
                uint8_t expected = ber_prbs_byte(config->prbs_order);
                uint8_t diff = rx_buf[i] ^ expected;
                while (diff) {
                    result->bit_errors += diff & 1;
                    diff >>= 1;
                }
            }
        } else {
            result->sync_losses++;
            result->synced = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    result->elapsed_seconds = (float)(esp_timer_get_time() - start) / 1000000.0f;
    result->bit_error_rate = (result->total_bits_received > 0) ?
        (double)result->bit_errors / result->total_bits_received : 0.0;
    result->test_complete = true;

    s_running = false;
    ESP_LOGI(TAG, "BER test complete: errors=%llu/%llu, BER=%.2e",
             result->bit_errors, result->total_bits_received, result->bit_error_rate);
    return SIG_OK;
}

sig_err_t tester_stop_ber(void)
{
    s_running = false;
    return SIG_OK;
}

sig_err_t tester_get_ber_status(ber_result_t *result)
{
    (void)result;
    return SIG_OK;
}

sig_err_t tester_run_stress(const stress_config_t *config, test_result_t *result)
{
    if (s_running) return SIG_ERR_BUSY;
    s_running = true;

    test_config_t test_cfg = {
        .protocol = config->protocol,
        .packet_count = 0,  /* Will be calculated */
        .packet_size = 64,
        .interval_ms = 0,
        .timeout_ms = 1000,
        .verify_data = true,
        .measure_latency = true,
        .test_pattern = 1,  /* PRBS */
    };

    /* Calculate packet count from duration and load */
    test_cfg.packet_count = config->duration_sec * 1000;
    test_cfg.interval_ms = (uint32_t)(1000.0f / (config->load_percent / 100.0f * 100));

    sig_err_t err = tester_run_loopback(&test_cfg, LOOPBACK_EXTERNAL, result);
    return err;
}

sig_err_t tester_stop_stress(void)
{
    s_running = false;
    return SIG_OK;
}

/* ── Protocol-Specific Tests ─────────────────────────────────── */

sig_err_t tester_uart_loopback(uint32_t baud, uint32_t packets, test_result_t *result)
{
    uart_config_params_t cfg = {
        .baud_rate = baud, .data_bits = 8, .stop_bits = 1,
        .parity = 0, .flow_control = false, .uart_num = 1,
    };
    protocol_init_driver(PROTO_UART, &cfg);

    test_config_t test = {
        .protocol = PROTO_UART, .packet_count = packets,
        .packet_size = 32, .interval_ms = 10, .timeout_ms = 500,
        .verify_data = true, .measure_latency = true, .test_pattern = 0,
    };
    return tester_run_loopback(&test, LOOPBACK_EXTERNAL, result);
}

sig_err_t tester_spi_loopback(uint32_t clock, uint32_t packets, test_result_t *result)
{
    spi_config_params_t cfg = {
        .clock_hz = clock, .mode = 0, .bit_order = 0,
        .cs_pin = PIN_SPI_CS0, .full_duplex = true,
    };
    protocol_init_driver(PROTO_SPI, &cfg);

    test_config_t test = {
        .protocol = PROTO_SPI, .packet_count = packets,
        .packet_size = 32, .interval_ms = 5, .timeout_ms = 500,
        .verify_data = true, .measure_latency = true, .test_pattern = 0,
    };
    return tester_run_loopback(&test, LOOPBACK_EXTERNAL, result);
}

sig_err_t tester_i2c_probe(uint8_t start_addr, uint8_t end_addr, test_result_t *result)
{
    result->protocol = PROTO_I2C;
    result->total_packets = 0;
    result->error_packets = 0;

    protocol_driver_t *drv = protocol_get_driver(PROTO_I2C);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    for (uint8_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t buf[2] = { addr, 0x00 };
        sig_err_t err = drv->send(buf, 1);
        result->total_packets++;
        if (err != SIG_OK) result->error_packets++;
    }
    return SIG_OK;
}

sig_err_t tester_can_echo(uint32_t baud, uint32_t packets, test_result_t *result)
{
    can_config_params_t cfg = {
        .baud_rate = baud, .listen_only = false,
        .filter_id = 0, .filter_mask = 0, .extended_frame = false,
    };
    protocol_init_driver(PROTO_CAN, &cfg);

    test_config_t test = {
        .protocol = PROTO_CAN, .packet_count = packets,
        .packet_size = 12, .interval_ms = 10, .timeout_ms = 500,
        .verify_data = true, .measure_latency = true, .test_pattern = 0,
    };
    return tester_run_loopback(&test, LOOPBACK_EXTERNAL, result);
}

sig_err_t tester_modbus_poll(uint8_t slave_id, uint16_t reg, uint16_t count,
                              test_result_t *result)
{
    result->protocol = PROTO_MODBUS_RTU;
    result->total_packets = 0;
    result->error_packets = 0;

    protocol_driver_t *drv = protocol_get_driver(PROTO_MODBUS_RTU);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    for (uint16_t i = 0; i < count; i++) {
        uint8_t request[6] = {
            slave_id, 0x03,
            ((reg + i) >> 8) & 0xFF, (reg + i) & 0xFF,
            0x00, 0x01,
        };
        sig_err_t err = drv->send(request, sizeof(request));
        result->total_packets++;
        if (err != SIG_OK) {
            result->error_packets++;
            continue;
        }

        uint8_t response[256];
        uint16_t resp_len = sizeof(response);
        err = drv->receive(response, &resp_len, 1000);
        if (err != SIG_OK) result->error_packets++;
    }
    return SIG_OK;
}

bool tester_is_running(void)
{
    return s_running;
}

sig_err_t tester_get_progress(float *percent)
{
    if (!percent) return SIG_ERR_INVALID_PARAM;
    *percent = s_progress;
    return SIG_OK;
}

sig_err_t tester_generate_report(char *buf, uint32_t buf_size, uint32_t *written)
{
    int pos = snprintf(buf, buf_size,
        "{\n"
        "  \"protocol\": \"%s\",\n"
        "  \"total_packets\": %lu,\n"
        "  \"error_packets\": %lu,\n"
        "  \"dropped_packets\": %lu,\n"
        "  \"bit_error_rate\": %.8f,\n"
        "  \"throughput_bps\": %.2f,\n"
        "  \"latency\": {\n"
        "    \"min_us\": %lu,\n"
        "    \"max_us\": %lu,\n"
        "    \"avg_us\": %lu\n"
        "  },\n"
        "  \"duration_ms\": %lu\n"
        "}\n",
        protocol_get_name(s_last_result.protocol),
        s_last_result.total_packets,
        s_last_result.error_packets,
        s_last_result.dropped_packets,
        s_last_result.bit_error_rate,
        s_last_result.throughput_bps,
        s_last_result.min_latency_us,
        s_last_result.max_latency_us,
        s_last_result.avg_latency_us,
        s_last_result.test_duration_ms);
    *written = (uint32_t)pos;
    return SIG_OK;
}
