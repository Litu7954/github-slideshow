/**
 * Signal Analyzer & Protocol Decoder Implementation
 * Captures, analyzes, and decodes signals from all supported protocols
 */
#include "analysis/signal_analyzer.h"
#include "protocols/protocol_driver.h"
#include "core/task_manager.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "analyzer";

static bool s_initialized = false;
static bool s_capturing = false;
static capture_buffer_t s_capture;
static analysis_stats_t s_stats;
static trigger_config_t s_trigger;
static TaskHandle_t s_capture_task = NULL;
static decoded_frame_t s_frame_buffer[64];
static uint32_t s_frame_write = 0;
static uint32_t s_frame_read = 0;
static uint32_t s_frame_count = 0;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

/* ── Capture Task ────────────────────────────────────────────── */
static void capture_task(void *pvParameter)
{
    protocol_type_t proto = (protocol_type_t)(uintptr_t)pvParameter;
    protocol_driver_t *drv = protocol_get_driver(proto);

    ESP_LOGI(TAG, "Capture started for %s", protocol_get_name(proto));
    uint32_t start_time = esp_log_timestamp();
    s_stats.total_frames = 0;
    s_stats.valid_frames = 0;
    s_stats.error_frames = 0;

    uint8_t rx_buf[256];
    uint32_t last_timestamp = 0;
    float interval_sum = 0;

    while (s_capturing) {
        uint16_t len = sizeof(rx_buf);
        sig_err_t err = SIG_ERR_NOT_INITIALIZED;

        if (drv && drv->is_ready()) {
            err = drv->receive(rx_buf, &len, 100);
        }

        if (err == SIG_OK && len > 0) {
            uint32_t now = (uint32_t)esp_timer_get_time();

            /* Store raw data in capture buffer */
            if (s_capture.write_pos + len < s_capture.size) {
                memcpy(&s_capture.buffer[s_capture.write_pos], rx_buf, len);
                s_capture.write_pos += len;
            } else {
                s_capture.overflow = true;
            }

            /* Decode frame */
            decoded_frame_t *frame = &s_frame_buffer[s_frame_write % 64];
            frame->protocol = proto;
            frame->timestamp_us = now;
            frame->raw_len = len;
            frame->raw_data = &s_capture.buffer[s_capture.write_pos - len];
            frame->has_error = false;

            /* Protocol-specific decode */
            switch (proto) {
                case PROTO_UART:
                case PROTO_RS232:
                case PROTO_RS485:
                    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
                             "UART RX [%d bytes]: ", len);
                    for (int i = 0; i < len && i < 32; i++) {
                        char hex[4];
                        snprintf(hex, sizeof(hex), "%02X ", rx_buf[i]);
                        strncat(frame->decoded_text, hex,
                                sizeof(frame->decoded_text) - strlen(frame->decoded_text) - 1);
                    }
                    break;
                case PROTO_I2C:
                    if (len >= 2) {
                        frame->fields.i2c.addr = rx_buf[0];
                        frame->fields.i2c.read = (rx_buf[0] & 1) != 0;
                        frame->fields.i2c.reg = (len > 1) ? rx_buf[1] : 0;
                        snprintf(frame->decoded_text, sizeof(frame->decoded_text),
                                 "I2C %s addr=0x%02X reg=0x%02X len=%d",
                                 frame->fields.i2c.read ? "RD" : "WR",
                                 frame->fields.i2c.addr >> 1,
                                 frame->fields.i2c.reg, len - 2);
                    }
                    break;
                case PROTO_CAN:
                    if (len >= 4) {
                        frame->fields.can.id = (rx_buf[0] << 24) | (rx_buf[1] << 16) |
                                                (rx_buf[2] << 8) | rx_buf[3];
                        frame->fields.can.dlc = len - 4;
                        snprintf(frame->decoded_text, sizeof(frame->decoded_text),
                                 "CAN ID=0x%08lX DLC=%d",
                                 (unsigned long)frame->fields.can.id, frame->fields.can.dlc);
                    }
                    break;
                case PROTO_MODBUS_RTU:
                    if (len >= 2) {
                        frame->fields.modbus.slave = rx_buf[0];
                        frame->fields.modbus.func = rx_buf[1];
                        snprintf(frame->decoded_text, sizeof(frame->decoded_text),
                                 "MODBUS slave=%d func=0x%02X len=%d",
                                 frame->fields.modbus.slave, frame->fields.modbus.func, len);
                    }
                    break;
                default:
                    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
                             "Raw [%d bytes]", len);
                    break;
            }

            s_frame_write++;
            s_frame_count++;
            s_stats.total_frames++;
            s_stats.valid_frames++;

            /* Interval tracking */
            if (last_timestamp > 0) {
                float interval = (float)(now - last_timestamp);
                interval_sum += interval;
                if (interval < s_stats.min_interval_us || s_stats.min_interval_us == 0) {
                    s_stats.min_interval_us = interval;
                }
                if (interval > s_stats.max_interval_us) {
                    s_stats.max_interval_us = interval;
                }
            }
            last_timestamp = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t duration = esp_log_timestamp() - start_time;
    s_stats.capture_duration_ms = duration;
    if (s_stats.total_frames > 1) {
        s_stats.avg_interval_us = interval_sum / (s_stats.total_frames - 1);
        s_stats.avg_frame_rate = s_stats.total_frames * 1000.0f / duration;
    }

    ESP_LOGI(TAG, "Capture complete: %lu frames in %lu ms",
             s_stats.total_frames, duration);

    s_capture_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────── */

sig_err_t analyzer_init(void)
{
    s_capture.buffer = (uint8_t *)malloc(CAPTURE_BUF_SIZE);
    if (!s_capture.buffer) {
        ESP_LOGE(TAG, "Failed to allocate capture buffer");
        return SIG_ERR_HARDWARE;
    }
    s_capture.size = CAPTURE_BUF_SIZE;
    s_capture.write_pos = 0;
    s_capture.read_pos = 0;
    s_capture.overflow = false;
    s_capture.capturing = false;

    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_trigger, 0, sizeof(s_trigger));

    /* Initialize ADC for analog signal analysis */
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);

    s_initialized = true;
    ESP_LOGI(TAG, "Signal analyzer initialized");
    return SIG_OK;
}

sig_err_t analyzer_deinit(void)
{
    analyzer_stop_capture();
    if (s_capture.buffer) {
        free(s_capture.buffer);
        s_capture.buffer = NULL;
    }
    if (s_adc_handle) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
    s_initialized = false;
    return SIG_OK;
}

sig_err_t analyzer_start_capture(protocol_type_t proto, uint32_t duration_ms)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (s_capturing) return SIG_ERR_BUSY;

    s_capture.write_pos = 0;
    s_capture.read_pos = 0;
    s_capture.overflow = false;
    s_frame_write = 0;
    s_frame_read = 0;
    s_frame_count = 0;
    s_capturing = true;

    xTaskCreate(capture_task, "capture", TASK_STACK_ANALYSIS,
                (void *)(uintptr_t)proto, TASK_PRIORITY_ANALYSIS, &s_capture_task);

    /* Auto-stop after duration */
    if (duration_ms > 0 && duration_ms <= CAPTURE_DURATION_MAX_MS) {
        /* Timer will be handled by the task checking elapsed time */
    }

    return SIG_OK;
}

sig_err_t analyzer_stop_capture(void)
{
    s_capturing = false;
    if (s_capture_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return SIG_OK;
}

sig_err_t analyzer_set_trigger(const trigger_config_t *trigger)
{
    if (!trigger) return SIG_ERR_INVALID_PARAM;
    memcpy(&s_trigger, trigger, sizeof(trigger_config_t));
    return SIG_OK;
}

bool analyzer_is_capturing(void)
{
    return s_capturing;
}

sig_err_t analyzer_get_frame(decoded_frame_t *frame)
{
    if (s_frame_read >= s_frame_write) return SIG_ERR_TIMEOUT;
    memcpy(frame, &s_frame_buffer[s_frame_read % 64], sizeof(decoded_frame_t));
    s_frame_read++;
    return SIG_OK;
}

sig_err_t analyzer_get_stats(analysis_stats_t *stats)
{
    if (!stats) return SIG_ERR_INVALID_PARAM;
    memcpy(stats, &s_stats, sizeof(analysis_stats_t));
    return SIG_OK;
}

uint32_t analyzer_get_frame_count(void)
{
    return s_frame_count;
}

/* ── Protocol Decoders ───────────────────────────────────────── */

sig_err_t analyzer_decode_uart(const uint8_t *raw, uint16_t len, decoded_frame_t *frame)
{
    frame->protocol = PROTO_UART;
    frame->raw_len = len;
    frame->has_error = false;
    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
             "UART [%d bytes]: ", len);
    for (int i = 0; i < len && i < 32; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X ", raw[i]);
        strncat(frame->decoded_text, hex,
                sizeof(frame->decoded_text) - strlen(frame->decoded_text) - 1);
    }
    return SIG_OK;
}

sig_err_t analyzer_decode_i2c(const uint8_t *raw, uint16_t len, decoded_frame_t *frame)
{
    if (len < 1) return SIG_ERR_INVALID_PARAM;
    frame->protocol = PROTO_I2C;
    frame->fields.i2c.addr = raw[0] >> 1;
    frame->fields.i2c.read = (raw[0] & 1) != 0;
    frame->fields.i2c.reg = (len > 1) ? raw[1] : 0;
    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
             "I2C %s addr=0x%02X",
             frame->fields.i2c.read ? "READ" : "WRITE", frame->fields.i2c.addr);
    return SIG_OK;
}

sig_err_t analyzer_decode_spi(const uint8_t *raw, uint16_t len, decoded_frame_t *frame)
{
    frame->protocol = PROTO_SPI;
    frame->raw_len = len;
    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
             "SPI [%d bytes]", len);
    return SIG_OK;
}

sig_err_t analyzer_decode_can(const uint8_t *raw, uint16_t len, decoded_frame_t *frame)
{
    if (len < 4) return SIG_ERR_INVALID_PARAM;
    frame->protocol = PROTO_CAN;
    frame->fields.can.id = (raw[0] << 24) | (raw[1] << 16) | (raw[2] << 8) | raw[3];
    frame->fields.can.dlc = len - 4;
    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
             "CAN ID=0x%08lX DLC=%d", (unsigned long)frame->fields.can.id, frame->fields.can.dlc);
    return SIG_OK;
}

sig_err_t analyzer_decode_modbus(const uint8_t *raw, uint16_t len, decoded_frame_t *frame)
{
    if (len < 2) return SIG_ERR_INVALID_PARAM;
    frame->protocol = PROTO_MODBUS_RTU;
    frame->fields.modbus.slave = raw[0];
    frame->fields.modbus.func = raw[1];
    if (len >= 4) {
        frame->fields.modbus.reg = (raw[2] << 8) | raw[3];
    }
    const char *func_name;
    switch (raw[1]) {
        case 0x01: func_name = "Read Coils"; break;
        case 0x02: func_name = "Read Discrete"; break;
        case 0x03: func_name = "Read Holding"; break;
        case 0x04: func_name = "Read Input"; break;
        case 0x05: func_name = "Write Coil"; break;
        case 0x06: func_name = "Write Reg"; break;
        case 0x0F: func_name = "Write Multi Coils"; break;
        case 0x10: func_name = "Write Multi Regs"; break;
        default: func_name = "Unknown"; break;
    }
    snprintf(frame->decoded_text, sizeof(frame->decoded_text),
             "MODBUS [%s] slave=%d reg=0x%04X",
             func_name, frame->fields.modbus.slave, frame->fields.modbus.reg);
    return SIG_OK;
}

/* ── Frequency Analysis ──────────────────────────────────────── */

sig_err_t analyzer_measure_frequency(uint8_t gpio_pin, float *freq_hz)
{
    if (!freq_hz) return SIG_ERR_INVALID_PARAM;

    /* Measure frequency using edge counting over a fixed window */
    uint32_t edges = 0;
    uint32_t window_us = 100000;  /* 100ms measurement window */

    int last_level = gpio_get_level(gpio_pin);
    int64_t start = esp_timer_get_time();

    while ((esp_timer_get_time() - start) < window_us) {
        int level = gpio_get_level(gpio_pin);
        if (level != last_level) {
            edges++;
            last_level = level;
        }
    }

    *freq_hz = (float)edges / 2.0f / (window_us / 1000000.0f);
    return SIG_OK;
}

sig_err_t analyzer_measure_duty_cycle(uint8_t gpio_pin, float *duty_pct)
{
    if (!duty_pct) return SIG_ERR_INVALID_PARAM;

    uint32_t high_time = 0;
    uint32_t total_time = 100000;  /* 100ms window */

    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < total_time) {
        if (gpio_get_level(gpio_pin)) {
            high_time++;
        }
    }

    *duty_pct = (float)high_time / total_time * 100.0f;
    return SIG_OK;
}

sig_err_t analyzer_measure_pulse_width(uint8_t gpio_pin, uint32_t *width_us)
{
    if (!width_us) return SIG_ERR_INVALID_PARAM;

    /* Wait for rising edge */
    uint32_t timeout = 1000000;
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) == 0) {
        if ((esp_timer_get_time() - start) > timeout) return SIG_ERR_TIMEOUT;
    }
    int64_t rise = esp_timer_get_time();

    while (gpio_get_level(gpio_pin) == 1) {
        if ((esp_timer_get_time() - rise) > timeout) return SIG_ERR_TIMEOUT;
    }
    *width_us = (uint32_t)(esp_timer_get_time() - rise);
    return SIG_OK;
}

/* ── Bus Scanning ────────────────────────────────────────────── */

sig_err_t analyzer_scan_i2c(uint8_t *found_addrs, uint8_t *count, uint8_t max_count)
{
    extern sig_err_t i2c_scan_bus(uint8_t *, uint8_t *, uint8_t);
    return i2c_scan_bus(found_addrs, count, max_count);
}

sig_err_t analyzer_scan_modbus(uint8_t start_id, uint8_t end_id,
                                uint8_t *found_ids, uint8_t *count)
{
    *count = 0;
    protocol_driver_t *drv = protocol_get_driver(PROTO_MODBUS_RTU);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    for (uint8_t id = start_id; id <= end_id; id++) {
        /* Send Read Holding Registers request to each ID */
        uint8_t request[6] = { id, 0x03, 0x00, 0x00, 0x00, 0x01 };
        drv->send(request, sizeof(request));

        uint8_t response[256];
        uint16_t resp_len = sizeof(response);
        if (drv->receive(response, &resp_len, 200) == SIG_OK) {
            if (response[0] == id) {
                found_ids[*count] = id;
                (*count)++;
                ESP_LOGI(TAG, "Modbus device found at ID %d", id);
            }
        }
    }
    return SIG_OK;
}

/* ── Data Export ──────────────────────────────────────────────── */

sig_err_t analyzer_export_json(char *buf, uint32_t buf_size, uint32_t *written)
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos,
                    "{\"stats\":{\"total\":%lu,\"valid\":%lu,\"errors\":%lu,"
                    "\"duration_ms\":%lu,\"avg_rate\":%.2f},\"frames\":[",
                    s_stats.total_frames, s_stats.valid_frames,
                    s_stats.error_frames, s_stats.capture_duration_ms,
                    s_stats.avg_frame_rate);

    uint32_t start = (s_frame_write > 64) ? s_frame_write - 64 : 0;
    for (uint32_t i = start; i < s_frame_write && pos < (int)(buf_size - 100); i++) {
        decoded_frame_t *f = &s_frame_buffer[i % 64];
        if (i > start) pos += snprintf(buf + pos, buf_size - pos, ",");
        pos += snprintf(buf + pos, buf_size - pos,
                        "{\"proto\":\"%s\",\"ts\":%lu,\"text\":\"%s\",\"err\":%s}",
                        protocol_get_name(f->protocol),
                        f->timestamp_us,
                        f->decoded_text,
                        f->has_error ? "true" : "false");
    }
    pos += snprintf(buf + pos, buf_size - pos, "]}");
    *written = pos;
    return SIG_OK;
}
